#!/usr/bin/env bash
# run_tests.sh — DPDK's EAL needs hugepages reserved and a hugetlbfs mount
# in place *before* the process starts; there's no ctest hook for that
# setup, so this script does it explicitly and then runs the real
# correctness test and benchmark directly. Needs root (hugepage
# reservation and the hugetlbfs mount both require it).
set -euo pipefail

HUGE_DIR="${HUGE_DIR:-/mnt/huge}"
NR_HUGEPAGES="${NR_HUGEPAGES:-64}"  # 64 * 2MB = 128MB, enough for this repo's pools

echo "=== Reserving ${NR_HUGEPAGES} hugepages ==="
echo "${NR_HUGEPAGES}" > /proc/sys/vm/nr_hugepages
cat /proc/sys/vm/nr_hugepages

echo "=== Mounting hugetlbfs at ${HUGE_DIR} ==="
mkdir -p "${HUGE_DIR}"
mountpoint -q "${HUGE_DIR}" || mount -t hugetlbfs nodev "${HUGE_DIR}"
mount | grep huge

echo "=== Clearing stale EAL multi-process state ==="
rm -rf /var/run/dpdk

echo "=== Building ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo ""
echo "=== Running correctness test ==="
./build/test_dpdk_primitives --no-pci --huge-dir="${HUGE_DIR}" -l 0

echo ""
echo "=== Running rte_ring vs mpsc::MpscQueue comparison ==="
rm -rf /var/run/dpdk
./build/bench_ring_comparison --no-pci --huge-dir="${HUGE_DIR}" -l 0
