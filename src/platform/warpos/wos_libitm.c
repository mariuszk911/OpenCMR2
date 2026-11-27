/*
 * The SDK's libstdc++ (GCC 9) has transaction-safe clones of its exception
 * classes (cow-stdexcept.o). They reference libitm, GCC's transactional
 * memory runtime, which WarpOS does not have, and they run only inside a
 * __transaction block, which nothing in this program contains.
 *
 * The references are weak: a final link would set them to 0. The WarpOS link
 * is relocatable, and a branch to an absolute 0 cannot be converted, so they
 * are defined here instead. Reaching one is a bug: it says so and stops.
 * _ZGTtdlPv and _ZGTtnaj are the transactional clones of operator delete and
 * operator new[], under their mangled names.
 */
#include "port/sys.h"

#include <stdlib.h>

static void Unreachable(const char *name)
{
    Sys_Log("fatal: %s called: transactional memory is not available on WarpOS", name);
    abort();
}

void _ITM_addUserCommitAction(void) { Unreachable("_ITM_addUserCommitAction"); }
void _ITM_memcpyRnWt(void) { Unreachable("_ITM_memcpyRnWt"); }
void _ITM_memcpyRtWn(void) { Unreachable("_ITM_memcpyRtWn"); }
void _ITM_RU1(void) { Unreachable("_ITM_RU1"); }
void _ITM_RU4(void) { Unreachable("_ITM_RU4"); }
void _ZGTtdlPv(void) { Unreachable("operator delete (transaction clone)"); }
void _ZGTtnaj(void) { Unreachable("operator new[] (transaction clone)"); }
