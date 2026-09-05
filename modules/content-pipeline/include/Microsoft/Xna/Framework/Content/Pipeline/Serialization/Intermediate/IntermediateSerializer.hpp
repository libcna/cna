// SPDX-License-Identifier: MS-PL
#pragma once

// The class itself lives in detail/IntermediateSerializerCore.hpp so that the reader and writer
// headers can include it without a cycle; this header is the one a caller includes, and it pulls
// in the reader, the writer, the type-description system and the template serializers that
// `IntermediateSerializer::TypeSerializerFor<T>()` instantiates.
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/detail/IntermediateSerializerCore.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/BuiltInTypeSerializers.hpp"
