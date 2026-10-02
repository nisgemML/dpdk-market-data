# Limitations

| Area | Current | Not claimed |
|------|---------|-------------|
| EAL / core DPDK APIs | Real `rte_eal_init`, `rte_mempool`, `rte_mbuf`, `rte_ring` — each behavior checked against an explicit assertion, not just "didn't crash" | Multi-lcore / multi-core DPDK operation (this sandbox has 1 core; `-l 0` is the only lcore used anywhere) |
| Packet I/O | None attempted — not via a physical NIC, not via a virtual/software PMD (`af_packet`, `tap`), not simulated | Any form of packet receive or transmit through DPDK. This is the single largest gap between this repo and "a working kernel-bypass feed handler" |
| Kernel bypass benefit | Not measured, not claimed | The actual latency/throughput benefit DPDK provides over a kernel-mediated path — fundamentally requires a NIC bound to `uio_pci_generic`/`vfio-pci`, which this environment does not have |
| `rte_ring` vs `mpsc::MpscQueue` comparison | Real, same-workload, same-process, repeated-run comparison (see `BENCHMARK_RESULTS.md`) | A verdict on which queue is "better" in general — this is one workload, one (shared, single-core) environment, and tail latencies were noisy enough across runs that only the median result is treated as a stable finding |
| NUMA | `rte_socket_id()` used throughout (correctly returns node 0 on this single-node machine) | Multi-socket NUMA-aware ring/mempool placement — same single-node constraint as [cpp26-alloc](https://github.com/nisgemML/cpp26-alloc)'s NUMA work, same honest reason |

## What would need to change to close the packet-I/O gap

In order of how much it would actually prove, not how easy each is:

1. **Weakest but easiest:** DPDK's `af_packet` PMD — works over a regular
   Linux interface, no special hardware. Proves "I can wire DPDK's `ethdev`
   API to something," but `af_packet` still goes through the kernel's own
   socket path internally — it would **not** demonstrate real kernel
   bypass, just DPDK's API surface for RX/TX. Deliberately not built here
   specifically to avoid implying more than that would prove.
2. **Real, but needs hardware:** a DMA-capable NIC (Intel X520/X710,
   Mellanox ConnectX, Solarflare) bound to `vfio-pci`, with a DPDK PMD for
   that NIC. This is what the job postings that motivated this repo
   actually mean by "kernel bypass" — and it needs physical access to
   that hardware, which this environment does not have.
