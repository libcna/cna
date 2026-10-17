# WEBGPU-1 test helper: include the checksum module and run a single verification, so the driver can
# invoke it as a child process and observe whether a mismatch fails closed. Params (via -D):
#   MODULE = path to cmake/WebGPUChecksum.cmake ; FILE = archive ; HASH = expected sha256 ; STAMP = stamp
include("${MODULE}")
cna_webgpu_verify_sha256("${FILE}" "${HASH}" "${STAMP}")
