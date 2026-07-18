// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Xnb/CurveContentTypeReader.hpp"
#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/ModelContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"
#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"
#include "CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"

namespace CNA::Internal::Xnb
{
    void RegisterAllBuiltInXnbReaders()
    {
        RegisterPrimitiveXnbReaders();
        RegisterMathXnbReaders();
        RegisterDecimalDateTimeXnbReaders();
        RegisterCurveXnbReader();
        RegisterTexture2DXnbReader();
        RegisterTexture3DXnbReader();
        RegisterTextureCubeXnbReader();
        RegisterSpriteFontXnbReader();
        RegisterSoundEffectXnbReader();
        RegisterSongXnbReader();
        RegisterVideoXnbReader();
        RegisterStockEffectXnbReaders();
        RegisterModelXnbReaders();
        Microsoft::Xna::Framework::Content::RegisterKnownUnsupportedXnbReaders();
    }
}
