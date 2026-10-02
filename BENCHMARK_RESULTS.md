# Benchmark Results — dpdk-market-data

Same honesty framework as the rest of this portfolio: single shared
vCPU sandbox (`nproc`=1), no `isolcpus`, no dedicated hardware. This is
Tier 1 (sandbox) by the tiering convention established in
[mpsc-queue](https://github.com/nisgemML/mpsc-queue)'s own
`BENCHMARK_RESULTS.md` — a smoke test with directional information, not
a number to publish as a latency claim.

## `rte_ring` vs `mpsc::MpscQueue` — all 5 runs, not one

Same process, same workload, same 100,000-event run, back to back,
across 5 separate invocations (not 5 samples within one run — 5 full
process restarts, clearing DPDK's multi-process state each time):

```
run 1: mpsc::MpscQueue  p50=3886ns  p90=4250ns  p99=8671ns   p99.9= 97710ns
       DPDK rte_ring     p50=3722ns  p90=3920ns  p99=8877ns   p99.9=136827ns

run 2: mpsc::MpscQueue  p50=2919ns  p90=3465ns  p99=4834ns   p99.9= 22640ns
       DPDK rte_ring     p50=2779ns  p90=2875ns  p99=4084ns   p99.9= 37242ns

run 3: mpsc::MpscQueue  p50=2919ns  p90=3463ns  p99=7491ns   p99.9= 35039ns
       DPDK rte_ring     p50=2782ns  p90=2887ns  p99=3900ns   p99.9= 30200ns

run 4: mpsc::MpscQueue  p50=2889ns  p90=3219ns  p99=4238ns   p99.9= 21605ns
       DPDK rte_ring     p50=2794ns  p90=3612ns  p99=6898ns   p99.9= 35713ns

run 5: mpsc::MpscQueue  p50=2909ns  p90=3513ns  p99=4292ns   p99.9= 44845ns
       DPDK rte_ring     p50=2777ns  p90=2865ns  p99=3976ns   p99.9= 25399ns
```

**What's actually stable across all 5 runs:** `rte_ring`'s p50 beats
`mpsc::MpscQueue`'s p50 in every single run, by a consistent, modest
margin (roughly 100-160ns, ~4-5%). That's the one finding treated as
real in the README.

**What's not stable, and is explicitly not claimed as a finding:** p90,
p99, and p99.9 flip back and forth between which queue is ahead,
run to run — `rte_ring` wins p90 in 4 of 5 runs but loses it in run 4;
p99 and p99.9 have no consistent winner across the 5 runs at all. This
is the signature of single-core OS scheduling noise dominating the tail
of the distribution, not a real property of either queue's design —
the same pattern documented throughout this portfolio's other
single-core benchmarks (see e.g. `tick-to-trade`'s own
`BENCHMARK_RESULTS.md`).

## Correctness (`test_dpdk_primitives`)

Not a benchmark, but worth recording here too: 5 repeated runs, 0
failures each time. Specifically checked, not just "didn't crash":
mempool `avail_count()` accounting through 16 allocs + 16 frees nets
back to the starting count; an `mbuf`'s payload round-trips byte-exact
through alloc → write → read → free; `rte_ring` preserves strict FIFO
order across 50 enqueue/dequeue pairs; a 4-slot ring correctly rejects
enqueues past capacity rather than silently accepting them.

## What a real multi-core / real-NIC run would need to show instead

This sandbox cannot produce the two numbers that would actually matter
for a kernel-bypass claim: (1) `rte_ring` throughput/latency under real
multi-core contention — DPDK's ring is designed for exactly that
scenario, and a single-core measurement undersells what it's built for;
(2) actual packet RX/TX latency via a DMA-capable NIC bound to
`vfio-pci`, which is the entire point of DPDK and is not attempted here
at all (see `LIMITATIONS.md`). Both would need real, different hardware
than what produced every number in this document.
