// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_xnb.md XNB-46: before this file existed, a real CNA game had no single call site to get
// every built-in .xnb ContentTypeReader registered -- ContentTypeReaderManager's registry
// (XNB-14/14A) starts empty by design (CNA has no reflection fallback, unlike FNA), and each
// Phase A-F reader family exposes its own individual Register*XnbReader()/RegisterXXXReaders()
// free function, called piecemeal by test fixtures' own SetUp(). A game wanting to actually load
// real .xnb assets would have had to discover and call all thirteen of those internal functions
// itself. This header is that missing single call site.
//
// Deliberately NOT auto-invoked from ContentManager's constructor: many existing tests rely on
// ContentTypeReaderManager::ClearTypeCreators() (in their own SetUp()) to exercise "reader not
// registered" error paths with a ContentManager constructed afterward; auto-registering everything
// on every ContentManager construction would silently repopulate the registry out from under that
// isolation. Call RegisterAllBuiltInXnbReaders() explicitly, once, at real game startup instead.

namespace CNA::Internal::Xnb
{
    /**
     * @brief Registers every built-in .xnb ContentTypeReader CNA implements (Phase A through
     *        Phase F/D3: primitives, math, `Decimal`/`DateTime`/`TimeSpan`, `Curve`, `Texture2D`,
     *        `Texture3D`, `TextureCube`, `SpriteFont`, `SoundEffect`, `Song`, the 5 stock effects,
     *        and `Model`'s own supporting readers), plus the known-unsupported placeholder (the
     *        general `EffectReader`, XNB-32A). A real game calls this once at startup, before
     *        loading any `.xnb` asset.
     *
     * Idempotent and safe to call more than once: `ContentTypeReaderManager::AddTypeCreator()`
     * silently ignores a repeat registration of the same canonical name.
     *
     * Generic collection readers (`ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<TKey,TValue>`/
     * `NullableReader<T>`) cannot be registered open-ended because CNA has no reflection to resolve
     * `typeof(T)` at runtime. This function therefore includes the closed generic combinations
     * required by CNA's built-in readers and known primitive-only XNA content shapes. A game whose
     * `.xnb` uses a collection containing its own type must still register that specific closed
     * combination (see `CollectionContentTypeReaders.hpp`'s design note).
     */
    void RegisterAllBuiltInXnbReaders();
}
