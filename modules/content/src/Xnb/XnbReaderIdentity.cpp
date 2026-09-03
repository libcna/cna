// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbReaderIdentity.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        // The XNA 4.0 assembly identities, read verbatim out of the committed fixtures' own
        // type-reader tables (plans/plan_xnapipeline.md §2.5). They are file content, not
        // implementation detail: every one of them is visible in a hex dump of any XNA-4.0-era
        // .xnb and in the assembly references of any XNA project file.
        constexpr const char* kMscorlib =
            ", mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089";
        constexpr const char* kFramework =
            ", Microsoft.Xna.Framework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=842cf8be1de50553";
        constexpr const char* kFrameworkGraphics =
            ", Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=842cf8be1de50553";

        void AppendGenericArgumentList(
            std::string& text, const std::vector<XnbReaderIdentity>& arguments,
            const bool qualify)
        {
            if (arguments.empty()) { return; }
            text += '[';
            for (std::size_t index = 0u; index < arguments.size(); ++index)
            {
                if (index > 0u) { text += ','; }
                text += '[';
                text += arguments[index].targetBaseName;
                if (arguments[index].targetSharesGenericArguments)
                {
                    AppendGenericArgumentList(text, arguments[index].genericArguments, qualify);
                }
                if (qualify) { text += XnbAssemblyQualifier(arguments[index].targetAssembly); }
                text += ']';
            }
            text += ']';
        }
    }

    std::string XnbAssemblyQualifier(const XnbAssembly assembly)
    {
        switch (assembly)
        {
            case XnbAssembly::Mscorlib: return kMscorlib;
            case XnbAssembly::Framework: return kFramework;
            case XnbAssembly::FrameworkGraphics: return kFrameworkGraphics;
            case XnbAssembly::None: break;
        }
        return {};
    }

    std::string FormatXnbReaderName(const XnbReaderIdentity& identity,
                                     const XnbReaderNameStyle style)
    {
        const bool qualify = style == XnbReaderNameStyle::Xna40;
        std::string text = identity.readerBaseName;
        AppendGenericArgumentList(text, identity.genericArguments, qualify);
        if (qualify) { text += XnbAssemblyQualifier(identity.readerAssembly); }
        return text;
    }

    std::string XnbCanonicalReaderName(const XnbReaderIdentity& identity)
    {
        std::string text = identity.readerBaseName;
        AppendGenericArgumentList(text, identity.genericArguments, false);
        return text;
    }

    std::string XnbTargetTypeName(const XnbReaderIdentity& identity)
    {
        std::string text = identity.targetBaseName;
        if (identity.targetSharesGenericArguments)
        {
            AppendGenericArgumentList(text, identity.genericArguments, false);
        }
        return text;
    }
}
