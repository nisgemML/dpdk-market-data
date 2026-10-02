# Bugs found while building this

Real mistakes caught while building, not a hypothetical list.

---

### 1. Reserved hugepages silently reset between build steps

**Symptom:** The first `rte_eal_init()` call of this project succeeded
(hugepages reserved, `hugetlbfs` mounted). A later test, built and run
separately, failed with `EAL: Cannot get hugepage information.` —
despite nothing in the test code touching hugepage configuration at all.
**Root cause:** `/proc/sys/vm/nr_hugepages` is sandbox-wide, ephemeral
kernel state — reserving it once in one shell session doesn't persist
across however the test environment resets state between tool
invocations, and a prior `hugetlbfs` mount point was no longer mounted
either when checked. Nothing in DPDK or this repo's own code was at
fault; the assumption that "I set this up once already" was.
**Fix:** `run_tests.sh` and the CI workflow both explicitly redo the
full setup (reserve, mount, clear stale `/var/run/dpdk` multi-process
state) every single run, rather than assuming any of it persists from a
previous step — the same operational discipline real DPDK deployments
already require, not a sandbox-specific workaround.
**Lesson:** "it worked a few commands ago" is not the same claim as "it
works," and this project's own test setup now makes that distinction
explicit instead of assuming continuity that isn't guaranteed.

### 2. Missing `#include <rte_errno.h>` — `rte_errno` isn't pulled in transitively

**Symptom:** `rte_errno` undeclared, despite `rte_mempool.h` and other
core DPDK headers already being included.
**Fix:** DPDK's error-reporting macro lives in its own header
(`rte_errno.h`) and isn't pulled in transitively by the APIs that
actually set it — added the explicit include. One line; the point of
recording it is that "the header that defines the function must also
declare its error variable" isn't actually guaranteed in C APIs the way
it often is in more tightly-coupled C++ libraries, and assuming
otherwise cost a build cycle to find.

### 3. Wrong assumption about `rte_mempool_avail_count()` on a freshly-created pool

**Symptom:** `test_dpdk_primitives` failed its very first assertion:
expected a freshly-created 4096-object pool to report `avail_count() ==
4095` (guessing DPDK reserves one internal bookkeeping slot, a pattern
seen in some other lock-free pool implementations). Actual value: 4096,
exactly the requested size.
**Fix:** wrote a 10-line standalone program to print the real value
instead of guessing a second time, confirmed `avail_count() ==
kPoolSize` empirically, and corrected the assertion to match reality.
**Lesson:** this is a small thing, and that's the point of recording it:
it's the same mistake-category as mpsc-queue's and tick-to-trade's
larger bugs — asserting what a system *should* do, based on a plausible
pattern from a different library, instead of checking what it *actually*
does. A guessed assertion that happens to be wrong is just as much a bug
as the code it's supposed to be checking, and the fix here (write a
10-line program and look) is the same discipline this whole portfolio
has tried to apply at every scale, not just the dramatic ones.
