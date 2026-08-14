// SPDX-License-Identifier: MS-PL

#include <CNA/C/abi.h>
#include <CNA/C/core.h>
#include <CNA/C/cna.h>

static_assert(CNA_ABI_VERSION == CNA_ABI_VERSION_ENCODE(0, 1, 0));
static_assert(sizeof(CNA_Result) == sizeof(uint32_t));
static_assert(sizeof(CNA_Handle) == sizeof(uint64_t));
static_assert(sizeof(CNA_ErrorCategory) == sizeof(uint32_t));
static_assert(sizeof(CNA_GameTime) == 24U);
static_assert(sizeof(CNA_Color) == 4U);
