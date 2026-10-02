// bench/bench_ring_comparison.cpp
//
// Direct, same-workload comparison between DPDK's own production rte_ring
// and this portfolio's own hand-rolled mpsc::MpscQueue (mpsc-queue,
// six-claim formal memory-ordering proof, 3.7-4.3x faster than mutex/
// spinlock/boost::lockfree::queue on real hardware per that repo's own
// BENCHMARK_RESULTS.md).
//
// Why this comparison is worth running at all: it's one thing to claim
// "I understand lock-free queues," another to show your own design next
// to the queue a real production HFT-grade framework actually ships,
// under the same workload, and report whichever way the number actually
// falls -- including if DPDK's wins, which is a perfectly reasonable,
// expected outcome (rte_ring is a mature, heavily-optimized production
// implementation; mpsc-queue is a portfolio piece built to demonstrate
// understanding, not to out-benchmark a production library).
//
// Single-producer, single-consumer, cross-thread push->pop latency.
// rte_ring is configured RING_F_SP_ENQ|RING_F_SC_DEQ (single-producer/
// single-consumer mode) to match mpsc::MpscQueue's own single-consumer
// contract exactly -- comparing rte_ring's general MPMC mode against a
// queue that's only ever single-consumer would not be the same workload.

#include "dpdkmd/market_event.hpp"
#include "mpsc/queue.hpp"

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ring.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Node : mpsc::MpscNode {
    dpdkmd::MarketEvent ev;
};

struct LatencyStats {
    std::vector<double> samples_ns;
    void add(double ns) { samples_ns.push_back(ns); }
    double percentile(double p) const {
        if (samples_ns.empty()) return 0.0;
        auto sorted = samples_ns;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
        return sorted[idx];
    }
};

uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

LatencyStats bench_mpsc_queue(uint64_t n) {
    mpsc::MpscQueue<Node> queue;
    auto nodes = std::make_unique<Node[]>(n);
    LatencyStats stats;
    std::atomic<bool> done{false};

    std::thread consumer([&] {
        uint64_t received = 0;
        while (received < n) {
            Node* node = queue.pop();
            if (node) {
                const uint64_t t1 = now_ns();
                stats.add(static_cast<double>(t1 - node->ev.recv_ns));
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });

    for (uint64_t i = 0; i < n; ++i) {
        nodes[i].ev.seq = i;
        nodes[i].ev.recv_ns = now_ns();
        queue.push(&nodes[i]);
        if (i % 64 == 0) std::this_thread::yield(); // avoid flooding ahead of the consumer
    }
    consumer.join();
    return stats;
}

LatencyStats bench_rte_ring(uint64_t n) {
    struct rte_ring* ring = rte_ring_create("bench_ring", 4096, rte_socket_id(),
                                             RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (!ring) {
        std::fprintf(stderr, "rte_ring_create failed: %s\n", rte_strerror(rte_errno));
        std::exit(1);
    }

    auto events = std::make_unique<dpdkmd::MarketEvent[]>(n);
    LatencyStats stats;

    std::thread consumer([&] {
        uint64_t received = 0;
        void* out = nullptr;
        while (received < n) {
            if (rte_ring_dequeue(ring, &out) == 0) {
                const uint64_t t1 = now_ns();
                auto* ev = static_cast<dpdkmd::MarketEvent*>(out);
                stats.add(static_cast<double>(t1 - ev->recv_ns));
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (uint64_t i = 0; i < n; ++i) {
        events[i].seq = i;
        events[i].recv_ns = now_ns();
        while (rte_ring_enqueue(ring, &events[i]) != 0) std::this_thread::yield(); // ring full -- retry, same backpressure story as everywhere else in this portfolio
        if (i % 64 == 0) std::this_thread::yield();
    }
    consumer.join();
    rte_ring_free(ring);
    return stats;
}

void print_stats(const char* label, const LatencyStats& s) {
    std::printf("%-16s n=%-8zu p50=%6.0fns  p90=%6.0fns  p99=%6.0fns  p99.9=%7.0fns\n",
                label, s.samples_ns.size(), s.percentile(0.50), s.percentile(0.90),
                s.percentile(0.99), s.percentile(0.999));
}

int main(int argc, char** argv) {
    const int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        std::fprintf(stderr, "rte_eal_init failed -- see README.md for required hugepage setup\n");
        return 1;
    }

    constexpr uint64_t kN = 100'000;
    std::printf("Cross-thread push->pop latency, single-producer/single-consumer, N=%lu\n\n", kN);

    // Run each twice; report the second (warmed) run, same convention
    // used throughout this portfolio's other benchmarks.
    (void)bench_mpsc_queue(kN);
    auto mpsc_stats = bench_mpsc_queue(kN);
    print_stats("mpsc::MpscQueue", mpsc_stats);

    (void)bench_rte_ring(kN);
    auto ring_stats = bench_rte_ring(kN);
    print_stats("DPDK rte_ring", ring_stats);

    std::printf(
        "\nSame environment/tier caveats as every other benchmark in this portfolio\n"
        "apply here too -- see BENCHMARK_RESULTS.md for which tier this run is and\n"
        "what that does and doesn't license concluding.\n");

    rte_eal_cleanup();
    return 0;
}
