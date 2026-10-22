# SPDX-License-Identifier: MS-PL
# Linux x86-64 diagnostic for the museum gs-avatar investigation.
# Run against an unstripped CNA test binary, e.g.:
# gdb -q -batch -x tools/graphics/avatar_load_lifetime.gdb --args \
#   cmake-build-multi/CnaGamerServicesTests \
#   --gtest_filter=AvatarRendererTest.CachedCompletionReleasesItsLoadOnlyAfterUnlocking
# The System V ABI passes free/unlock's pointer in rdi. This is deliberately a
# debugger-only check, not a production hook or a portable unit-test replacement.
set pagination off
set confirm off
set breakpoint pending on
set print thread-events off
set $avatar_mutex = (void*)0
set $avatar_allocation = (void*)0
set $avatar_freed = 0
set $avatar_observed = 0
set $avatar_thread = 0
break Microsoft::Xna::Framework::GamerServices::AvatarRenderer::Poll
commands
  silent
  if resources_->load._M_ptr != 0
    set $avatar_mutex = (void*)&resources_->load._M_ptr->lock
    set $avatar_allocation = (void*)resources_->load._M_refcount._M_pi
    set $avatar_freed = 0
    set $avatar_thread = $_thread
  end
  continue
end
break __libc_free
commands
  silent
  if (void*)$rdi == $avatar_allocation && $avatar_allocation != 0
    set $avatar_freed = 1
  end
  continue
end
break pthread_mutex_unlock
commands
  silent
  if (void*)$rdi == $avatar_mutex && $avatar_mutex != 0 && $_thread == $avatar_thread
    set $avatar_observed = $avatar_observed + 1
    if $avatar_freed
      printf "FAIL: AvatarRenderer unlocks its AvatarLoad mutex after freeing the owning allocation\n"
      bt 8
      quit 1
    end
    set $avatar_mutex = (void*)0
    set $avatar_allocation = (void*)0
  end
  continue
end
run
if $avatar_observed == 0
  echo INCONCLUSIVE: no AvatarLoad mutex unlock observed\n
  quit 2
end
if $_exitcode != 0
  quit 3
end
printf "PASS: %d observed AvatarLoad mutex unlocks preceded allocation release\n", $avatar_observed
