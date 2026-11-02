// SPDX-License-Identifier: MS-PL
extern int cna_probe(void);
int main(void) { return cna_probe() == 22 ? 0 : 1; }
