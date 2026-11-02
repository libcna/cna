// SPDX-License-Identifier: MS-PL
extern int Implementation();
extern int Opaque();
int main() { return Implementation() > 0 && Opaque() == 22 ? 0 : 1; }
