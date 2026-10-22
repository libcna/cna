// SPDX-License-Identifier: MS-PL
#ifdef CNA_PROBE_PRODUCER
#error Producer-only flags leaked into the installed consumer.
#endif
#if defined(PROBE_EXCEPTION_TEST) && (!defined(CNA_PROBE_CONFIG_A) || !defined(CNA_PROBE_CONFIG_B))
#error The installed header-only consumer lost nested configuration options.
#endif
int probe_add(int, int);
void probe_throw();
int main()
{
#ifdef PROBE_EXCEPTION_TEST
    try { probe_throw(); }
    catch (int value) { return value == probe_add(19, 23) ? 0 : 1; }
    return 2;
#else
    return probe_add(19, 23) == 42 ? 0 : 1;
#endif
}
