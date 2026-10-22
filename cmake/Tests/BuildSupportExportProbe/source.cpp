// SPDX-License-Identifier: MS-PL
#ifndef CNA_PROBE_PRODUCER
#error The source consumer lost its header-only build support.
#endif
#if !defined(CNA_PROBE_CONFIG_A) || !defined(CNA_PROBE_CONFIG_B)
#error The source consumer lost nested configuration options.
#endif
int probe_add(int, int);
void probe_throw();
int main()
{
    try { probe_throw(); }
    catch (int value) { return value == probe_add(19, 23) ? 0 : 1; }
    return 2;
}
