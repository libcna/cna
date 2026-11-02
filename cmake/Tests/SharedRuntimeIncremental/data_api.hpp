// SPDX-License-Identifier: MS-PL
#pragma once
/** @brief Data whose extent is deliberately private to the runtime. */
extern int ExportedData[];
/**
 * @brief Verify the complete exported object inside the runtime.
 * @return Whether all runtime-owned elements have their expected values.
 */
bool VerifyData();
