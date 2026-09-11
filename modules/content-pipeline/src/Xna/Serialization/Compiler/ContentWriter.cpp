// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentWriter.hpp"

#include <span>
#include <system_error>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    ContentWriter::ContentWriter(const ContentCompiler& compiler, CNA::Internal::Xnb::XnbWriter& output,
                                 const TargetPlatform targetPlatform,
                                 const Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile,
                                 std::filesystem::path outputDirectory, std::string assetName)
        : ContentWriterPlaceholderStream()
        , System::IO::BinaryWriter(&stream, true)
        , compiler_(&compiler)
        , output_(&output)
        , targetPlatform_(targetPlatform)
        , targetProfile_(targetProfile)
        , outputDirectory_(std::move(outputDirectory))
        , assetName_(std::move(assetName))
    {
        compiler_->RegisterActive(output, *this);
    }

    ContentWriter::~ContentWriter()
    {
        Dispose(true);
    }

    TargetPlatform ContentWriter::getTargetPlatformProperty() const noexcept { return targetPlatform_; }

    Microsoft::Xna::Framework::Graphics::GraphicsProfile ContentWriter::getTargetProfileProperty() const noexcept
    {
        return targetProfile_;
    }

    void ContentWriter::Write(const SharpRuntime::bytecs value) { output_->WriteByte(value); }
    void ContentWriter::Write(const std::int8_t value) { output_->WriteSByte(value); }
    void ContentWriter::Write(const SharpRuntime::shortcs value) { output_->WriteInt16(value); }
    void ContentWriter::Write(const SharpRuntime::ushortcs value) { output_->WriteUInt16(value); }
    void ContentWriter::Write(const SharpRuntime::intcs value) { output_->WriteInt32(value); }
    void ContentWriter::Write(const std::uint32_t value) { output_->WriteUInt32(value); }
    void ContentWriter::Write(const SharpRuntime::longcs value) { output_->WriteInt64(value); }
    void ContentWriter::Write(const std::uint64_t value) { output_->WriteUInt64(value); }
    void ContentWriter::Write(const SharpRuntime::Single value) { output_->WriteSingle(value); }
    void ContentWriter::Write(const double value) { output_->WriteDouble(value); }
    void ContentWriter::Write(const bool value) { output_->WriteBoolean(value); }
    void ContentWriter::Write(const std::string& value) { output_->WriteString(value); }

    void ContentWriter::Write(const SharpRuntime::bytecs* buffer, const SharpRuntime::intcs offset,
                              const SharpRuntime::intcs count)
    {
        if (buffer == nullptr || offset < 0 || count < 0)
        {
            throw PipelineException("ContentWriter::Write(byte[]): a null buffer or negative range.");
        }
        output_->WriteBytes(std::span<const std::uint8_t>(buffer + offset, static_cast<std::size_t>(count)));
    }

    void ContentWriter::Write(const SharpRuntime::charcs value) { output_->WriteChar(value); }
    void ContentWriter::Write7BitEncodedInt(const SharpRuntime::intcs value) { output_->Write7BitEncodedInt(value); }
    void ContentWriter::Write(const Microsoft::Xna::Framework::Color& value) { output_->WriteColor(value); }
    void ContentWriter::Write(const Microsoft::Xna::Framework::Matrix& value) { output_->WriteMatrix(value); }
    void ContentWriter::Write(const Microsoft::Xna::Framework::Quaternion& value) { output_->WriteQuaternion(value); }
    void ContentWriter::Write(const Microsoft::Xna::Framework::Vector2& value) { output_->WriteVector2(value); }
    void ContentWriter::Write(const Microsoft::Xna::Framework::Vector3& value) { output_->WriteVector3(value); }
    void ContentWriter::Write(const Microsoft::Xna::Framework::Vector4& value) { output_->WriteVector4(value); }

    void ContentWriter::Flush() {}
    void ContentWriter::Close() {}

    CNA::Internal::Xnb::XnbWriter& ContentWriter::Output() const noexcept { return *output_; }

    void ContentWriter::Dispose(const bool disposing)
    {
        if (disposed_) { return; }
        disposed_ = true;
        if (disposing && compiler_ != nullptr) { compiler_->UnregisterActive(*output_); }
    }

    const XnaTypeWorker* ContentWriter::FindWorker(const std::type_index carrier) const
    {
        return compiler_->FindWorker(carrier, targetPlatform_);
    }

    const XnaTypeWorker& ContentWriter::RequireWorkerForObject(const System::Object& object,
                                                               const std::string& staticTypeName) const
    {
        const XnaTypeWorker* worker = compiler_->FindWorkerForObject(object, targetPlatform_);
        if (worker == nullptr)
        {
            throw PipelineException("No ContentTypeWriter is registered for '{0}' (declared as '{1}').",
                                    object.GetTypeName(), staticTypeName);
        }
        return *worker;
    }

    const XnaTypeWorker& ContentWriter::WorkerFor(const ContentTypeWriterBase& writer) const
    {
        const XnaTypeWorker* worker = compiler_->FindWorkerFor(writer, targetPlatform_);
        if (worker == nullptr)
        {
            throw PipelineException("The type writer for '{0}' does not belong to the compiler writing this file.",
                                    writer.GetRuntimeType(targetPlatform_));
        }
        return *worker;
    }

    void ContentWriter::RequireCarrier(const XnaTypeWorker& worker, const std::type_index carrier,
                                       const std::string& typeName)
    {
        if (worker.CarrierType() != carrier)
        {
            throw PipelineException("The type writer for '{0}' cannot write a value of type '{1}'.",
                                    worker.TypeName(), typeName);
        }
    }

    void ContentWriter::WriteExternalReferenceName(const std::string& filename)
    {
        if (filename.empty())
        {
            output_->WriteExternalReference("");
            return;
        }
        // XNA writes the referenced asset's path relative to the directory of the asset being
        // written, without the compiled extension.
        std::filesystem::path reference(filename);
        const std::filesystem::path assetFile =
            outputDirectory_.empty() ? std::filesystem::path(assetName_) : outputDirectory_ / assetName_;
        const std::filesystem::path baseDirectory = assetFile.parent_path();
        if (reference.is_absolute() && !baseDirectory.empty() && baseDirectory.is_absolute())
        {
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(reference, baseDirectory, error);
            if (!error && !relative.empty()) { reference = relative; }
        }
        else if (reference.is_relative() && !baseDirectory.empty() && baseDirectory.is_relative())
        {
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(reference, baseDirectory, error);
            if (!error && !relative.empty()) { reference = relative; }
        }
        if (reference.extension() == ".xnb") { reference.replace_extension(); }
        output_->WriteExternalReference(reference.generic_string());
    }
}
