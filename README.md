# dpdk-market-data

A real DPDK (Data Plane Development Kit) application using the actual EAL,
mempool, mbuf, and ring APIs — verified against the kernel, not just "it
compiled" — plus a direct, same-workload comparison against this
portfolio's own hand-rolled [mpsc-queue](https://github.com/nisgemML/mpsc-queue).

## Read this before the rest of the README

**What this proves:** genuine, correct use of DPDK's core data-plane
primitives — `rte_eal_init`, NUMA-local `rte_mempool`/`rte_mbuf`
allocation, and `rte_ring` with verified FIFO ordering and correct
full-ring behavior (`tests/test_dpdk_primitives.cpp`) — **and real packet
RX/TX**: `rte_eth_tx_burst()`/`rte_eth_rx_burst()` through DPDK's actual
ethdev API, a real Ethernet frame genuinely round-tripped through the
`net_af_packet` PMD bound to the `lo` interface, payload verified
byte-exact (`tests/test_afpacket_rxtx.cpp`). Every claim here is backed
by a test that checks an actual assertion, not just "didn't crash."

**What this does NOT prove, stated plainly rather than implied:**
DPDK's actual purpose — kernel-bypass packet I/O — requires a NIC bound
to a DMA-capable userspace driver (`uio_pci_generic`/`vfio-pci`). No such
hardware is available in the environment this was built and tested in.
`net_af_packet` is a **software** PMD — it still goes through the
kernel's own AF_PACKET socket path internally, the same as any ordinary
socket. The RX/TX test above proves the DPDK *programming model* works
correctly end-to-end; it does not demonstrate — and does not claim — the
actual zero-copy DMA benefit DPDK exists for, which fundamentally needs
hardware this environment doesn't have. See [`LIMITATIONS.md`](LIMITATIONS.md)
for the complete, explicit scope.

## Why this exists

Kernel bypass (DPDK) was an identified gap against top-tier HFT firm
requirements — this repo closes the "have I ever actually written and
run a real DPDK application" question honestly, rather than leaving it
as an unverified line on a resume. What it doesn't close, and can't
without real NIC hardware, is "have I measured the actual kernel-bypass
latency benefit" — that gap is still open and stated as such, not
papered over.

## The comparison: DPDK's `rte_ring` vs this portfolio's own `mpsc::MpscQueue`

Same workload (single-producer/single-consumer, cross-thread push→pop
latency, 100,000 events, identical 40-byte event payload), run back to
back in the same process:

```
mpsc::MpscQueue  n=100000   p50=  2909ns  p90=  3513ns  p99=  4292ns  p99.9= 44845ns
DPDK rte_ring    n=100000   p50=  2777ns  p90=  2865ns  p99=  3976ns  p99.9= 25399ns
```

**Reading this honestly:** DPDK's `rte_ring` is modestly, consistently
faster at the median across repeated runs (~4-5%) — a mature, heavily
optimized production implementation beating a portfolio piece is a
perfectly reasonable, expected result, not something to spin. Tail
latencies (p99, p99.9) are noisy in both directions across runs on this
single-core sandbox, dominated by OS scheduling, not a consistent
property of either queue — see `BENCHMARK_RESULTS.md` for the full
multi-run data rather than one cherry-picked pair of numbers.

## Build & run

Needs `libdpdk-dev` (23.11+) and root (hugepage reservation + hugetlbfs
mount both require it):

```bash
sudo apt-get install -y libdpdk-dev
sudo ./run_tests.sh
```

`run_tests.sh` does the EAL's actual prerequisites explicitly — reserve
hugepages, mount `hugetlbfs`, clear stale multi-process state — then
builds and runs the real correctness test and the comparison benchmark.
There's no `ctest` entry for the DPDK binaries specifically because EAL
initialization needs hugepages in place *before* the process starts,
which `ctest` has no hook for; CI and `run_tests.sh` invoke them
directly instead.

## What's in here

| Path | What it is |
|---|---|
| `include/dpdkmd/market_event.hpp` | Shared 40-byte event struct, shaped like the AddOrder events used elsewhere in this portfolio |
| `tests/test_dpdk_primitives.cpp` | Correctness: mempool accounting, mbuf round-trip, ring FIFO order, ring-full behavior — each a real assertion |
| `tests/test_afpacket_rxtx.cpp` | Real packet RX/TX: `rte_eth_tx_burst`/`rx_burst` over `net_af_packet` (`lo`), payload verified byte-exact |
| `bench/bench_ring_comparison.cpp` | The `rte_ring` vs `mpsc::MpscQueue` comparison above |
| `third_party/mpsc-queue/` | Vendored from [mpsc-queue](https://github.com/nisgemML/mpsc-queue) for the comparison — not reimplemented |

## Related

- [mpsc-queue](https://github.com/nisgemML/mpsc-queue) — the queue compared against here
- [tick-to-trade](https://github.com/nisgemML/tick-to-trade) — real UDP multicast receive (the networking half DPDK would eventually replace, on real NIC hardware)
- [cpp26-alloc](https://github.com/nisgemML/cpp26-alloc) — NUMA-aware, explicitly hugepage-backed allocation (same hugepage mechanism this repo also depends on, independently verified there)
