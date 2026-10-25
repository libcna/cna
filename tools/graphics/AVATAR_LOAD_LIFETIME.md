# Avatar load mutex lifetime regression

`AvatarRenderer::Poll` must unlock `AvatarLoad::lock` before releasing the shared
result that owns it. A cached model completes synchronously, so there is no worker
reference to keep that result alive. Previously `load.reset()` ran while the
lock_guard was still in scope, and its destructor unlocked freed storage.

`AvatarRendererTest.CachedCompletionReleasesItsLoadOnlyAfterUnlocking` retains a
cache primer, consumes a second renderer's synchronous result, compares all bind
transforms, and verifies that disposing the primer does not invalidate the second
renderer. Run it with the GamerServices tests and with memory/thread sanitizers.
Plain functional success alone does not establish the mutex's lifetime: an
uninstrumented libc may silently write through the freed mutex pointer.

The accompanying debugger observer checks the actual allocation/free/unlock order
without changing production code. It requires Linux x86-64, glibc, GNU libstdc++,
GDB and an unstripped Debug test binary. It uses the System V `rdi` argument register
and libstdc++ shared_ptr debug layout; it is not a portable production facility.
From the CNA repository, after building `CnaGamerServicesTests`:

```sh
CNA_GRAPHICS_RENDERER=SOFTWARE gdb -q -batch \
  -x tools/graphics/avatar_load_lifetime.gdb --args \
  cmake-build-multi/CnaGamerServicesTests \
  --gtest_filter=AvatarRendererTest.CachedCompletionReleasesItsLoadOnlyAfterUnlocking
```

This test does not create a graphics device or window. The observer records the
result's mutex and control-block allocation on entry to Poll. It rejects an
unlock by that polling thread after the allocation was freed, and forgets the
address after unlock so future address reuse is not misclassified. Exit 1 is a
lifetime failure; exit 2 means no matching unlock was observed; exit 3 means the
test process did not pass. A pass requires both an observed unlock and test exit 0.

During the museum investigation, the pre-repair existing regression
`AvatarRendererTest.ALoadCompletingInsideDrawWaitsUntilTheCallerCanUpdateItsAnimation`
also failed this observer: free preceded the lock_guard destructor in Poll. Both
observations are in the campaign reports `repair-museum-avatar-mutex-before-v1.log`
and `-v2.log`. This proves a CNA lifetime defect, **not** that it caused the museum's
one historical crash inside Mesa. The museum did not reproduce that crash in 40 more
`gs-avatar` press-all runs under glibc's heap checking at `9d49828f4`
(`../museum-simulator/docs/CNA_FINDINGS.md`).
