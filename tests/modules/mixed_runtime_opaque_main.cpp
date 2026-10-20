// SPDX-License-Identifier: MS-PL
// No direct CNA references: --as-needed must not defeat the shared-runtime ordering.
int CnaMixedRuntimeOpaqueFramework();
int main() { return CnaMixedRuntimeOpaqueFramework(); }
