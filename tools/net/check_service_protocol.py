# SPDX-License-Identifier: MS-PL
"""Compare CNA's vendored protocol parser/vectors with the canonical server source."""
import pathlib, sys
client = pathlib.Path(__file__).resolve().parents[2]
server = pathlib.Path(sys.argv[1]).resolve()
for original, copied in (
    ("protocol/include/CnaService/RelayProtocol.hpp", "modules/net/src/Internal/Protocol/CnaService/RelayProtocol.hpp"),
    ("src/RelayProtocol.cpp", "modules/net/src/Internal/RelayProtocol.cpp"),
    ("protocol/golden/relay-v1.json", "modules/net/tests/fixtures/relay-protocol-v1.json"),
    ("protocol/include/CnaService/Protocol.hpp", "modules/gamer-services/src/Internal/Protocol/CnaService/Protocol.hpp"),
    ("src/Protocol.cpp", "modules/gamer-services/src/Internal/ServiceProtocol.cpp"),
    ("protocol/golden/v1.json", "modules/gamer-services/tests/fixtures/service-protocol-v1.json"),
):
    if (server/original).read_bytes() != (client/copied).read_bytes():
        raise SystemExit("Protocol drift: " + original)
print("Protocol parser/header/golden vectors match canonical server")
