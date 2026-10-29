// SPDX-License-Identifier: MS-PL
#pragma once

// XNB-46: before this file existed, a real CNA game had no single call site to get
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
// isolation.
//
// A game does not call it either: ContentManager::Load<T>() registers the readers an asset of
// type T can need on its first use (detail::BuiltinContent in ContentManager.hpp), so a program
// links only the readers of the types it loads. This is the explicit everything-at-once entry
// point, for a type whose content may hold any asset and for tools.

namespace CNA::Internal::Xnb
{
    /**
     * @brief Registers every built-in .xnb ContentTypeReader CNA implements (Phase A through
     *        Phase F/D3: primitives, math, `Decimal`/`DateTime`/`TimeSpan`, `Curve`, `Texture2D`,
     *        `Texture3D`, `TextureCube`, `SpriteFont`, `SoundEffect`, `Song`, the 5 stock effects,
     *        and `Model`'s own supporting readers), plus the known-unsupported placeholder (the
     *        general `EffectReader`, XNB-32A). A `Game` declares them instead and each is installed
     *        when needed (ContentTypeReaderManager.hpp); this registers them all at once, a reader
     *        removed since included.
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
