// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Registers the `.xml` source route: XNA's intermediate XML, whatever it declares.
     *
     * XNA's `XmlImporter` reads a document whose `Asset Type` attribute names the type to build,
     * and the type may be one the framework defines or one the game does. XNA resolved a
     * game-defined type through that game's own pipeline assembly; C++ has no assembly to load, so
     * the equivalent is a type the game has registered with the @ref
     * Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentCompiler before
     * the build runs -- its serializer for reading it and its `ContentTypeWriter` for writing it.
     * Everything this route needed already existed; what was missing was the route
     * (plans/plan_xnapipeline_parity.md `XNAPP-260`).
     *
     * Three registrations, and no second build engine:
     *
     *   * the importer, under XNA's own `XmlImporter` name, declaring every type the compiler
     *     knows as an output type so an `.xml` asset takes part in incremental builds like any
     *     other source;
     *   * one pass-through processor per known type, which is what XNA's `PassThroughProcessor`
     *     is -- it returns its input unchanged. They carry distinct registry names because the
     *     registry keys a processor by name, and nothing has to speak those names: a project
     *     naming `PassThroughProcessor` reaches them through the component mapping's
     *     `chooseByImportedType`, and a project naming none reaches them because a type has
     *     exactly one;
     *   * the `.xnb` writers, through @ref RegisterXnaXnbOutput, which already writes any type
     *     the compiler knows.
     *
     * A game that registers its own type writer and its own processor before calling this gets the
     * same three registrations over its own type, which is the whole of what "custom XML content"
     * means here.
     *
     * There is no `.cnb` half. A `.cnb` has no schema for an arbitrary XNA object graph and does
     * not need one: this route exists to reproduce XNA, and an asset built through it is asked for
     * as `.xnb`. A `--format cnb` build of an `.xml` asset is refused by writer resolution, which
     * names the type it has no writer for.
     *
     * @param registry Mutable registry to configure before builds begin.
     * @param compiler The compiler whose known types this route can carry; add a game's own type
     *        writers to it before calling. Must not be null.
     * @param options Container options every registered `.xnb` writer emits.
     */
    void RegisterXnaXmlSourceContentPipeline(
        ContentPipelineRegistry& registry,
        std::shared_ptr<const Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentCompiler> compiler,
        const CNA::Internal::Xnb::XnbFileOptions& options);
}
