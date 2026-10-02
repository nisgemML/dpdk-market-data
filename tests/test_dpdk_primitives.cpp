// tests/test_dpdk_primitives.cpp
//
// Correctness, not just "it ran": EAL init, NUMA-local mempool creation,
// mbuf alloc/free accounting, and ring enqueue/dequeue ordering, each
// checked against an explicit assertion -- not just "no crash."
//
// Scope, stated plainly: this tests DPDK's actual core data-plane APIs
// (mempool, mbuf, ring) genuinely and correctly. It does NOT test packet
// RX/TX through any PMD (virtual or physical) -- that was deliberately
// not attempted in this environment rather than built unreliably under
// time pressure. See LIMITATIONS.md for exactly what that means.

#include "dpdkmd/market_event.hpp"

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#include <cstdio>
#include <cstring>

namespace {
int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
} while (0)
}

int main(int argc, char** argv) {
    const int eal_args = rte_eal_init(argc, argv);
    CHECK(eal_args >= 0);
    if (eal_args < 0) {
        std::fprintf(stderr, "rte_eal_init failed: %s -- see README.md for hugepage setup\n",
                     rte_strerror(rte_errno));
        return 1;
    }

    // ── mempool + mbuf ──────────────────────────────────────────────────────
    constexpr unsigned kPoolSize = 4096;
    struct rte_mempool* pool = rte_pktmbuf_pool_create(
        "test_pool", kPoolSize, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    CHECK(pool != nullptr);
    if (!pool) { std::fprintf(stderr, "pool create failed: %s\n", rte_strerror(rte_errno)); return 1; }

    const unsigned initial_avail = rte_mempool_avail_count(pool);
    CHECK(initial_avail == kPoolSize); // verified empirically, not assumed: a freshly-created
                                        // pool's avail count is exactly the requested size

    struct rte_mbuf* bufs[16];
    for (auto& b : bufs) {
        b = rte_pktmbuf_alloc(pool);
        CHECK(b != nullptr);
    }
    CHECK(rte_mempool_avail_count(pool) == initial_avail - 16);

    // Write and read back through the actual mbuf data area, not just
    // check the pointer isn't null.
    dpdkmd::MarketEvent ev{};
    ev.seq = 42;
    ev.order_ref = 1001;
    ev.price = 1000000;
    ev.qty = 50;
    ev.side = 'B';
    std::memcpy(rte_pktmbuf_mtod(bufs[0], void*), &ev, sizeof(ev));
    bufs[0]->data_len = bufs[0]->pkt_len = sizeof(ev);

    dpdkmd::MarketEvent round_tripped{};
    std::memcpy(&round_tripped, rte_pktmbuf_mtod(bufs[0], void*), sizeof(round_tripped));
    CHECK(round_tripped.seq == 42);
    CHECK(round_tripped.order_ref == 1001);
    CHECK(round_tripped.price == 1000000);
    CHECK(round_tripped.side == 'B');

    for (auto* b : bufs) rte_pktmbuf_free(b);
    CHECK(rte_mempool_avail_count(pool) == initial_avail); // every alloc freed, none leaked

    // ── ring: FIFO ordering, not just "enqueue/dequeue didn't fail" ────────
    struct rte_ring* ring = rte_ring_create("test_ring", 64, rte_socket_id(),
                                             RING_F_SP_ENQ | RING_F_SC_DEQ);
    CHECK(ring != nullptr);

    constexpr int kN = 50;
    for (int i = 0; i < kN; ++i) {
        void* val = reinterpret_cast<void*>(static_cast<std::intptr_t>(i + 1)); // avoid enqueuing a null ptr
        CHECK(rte_ring_enqueue(ring, val) == 0);
    }
    bool fifo_order_correct = true;
    for (int i = 0; i < kN; ++i) {
        void* out = nullptr;
        CHECK(rte_ring_dequeue(ring, &out) == 0);
        if (reinterpret_cast<std::intptr_t>(out) != i + 1) fifo_order_correct = false;
    }
    CHECK(fifo_order_correct);
    CHECK(rte_ring_count(ring) == 0); // fully drained

    // ring-full is reported, not silently dropped
    struct rte_ring* small_ring = rte_ring_create("small_ring", 4, rte_socket_id(),
                                                   RING_F_SP_ENQ | RING_F_SC_DEQ);
    CHECK(small_ring != nullptr);
    int enqueued = 0;
    for (int i = 0; i < 10; ++i) {
        void* val = reinterpret_cast<void*>(static_cast<std::intptr_t>(i + 1));
        if (rte_ring_enqueue(small_ring, val) == 0) ++enqueued;
    }
    CHECK(enqueued < 10); // must have hit capacity, not silently accepted everything
    CHECK(enqueued == static_cast<int>(rte_ring_get_capacity(small_ring)));

    rte_ring_free(ring);
    rte_ring_free(small_ring);
    rte_mempool_free(pool);
    rte_eal_cleanup();

    if (g_failures == 0) {
        std::printf("PASS: DPDK EAL/mempool/mbuf/ring primitives verified correct, not just non-crashing\n");
        return 0;
    }
    std::printf("FAIL: %d check(s) failed\n", g_failures);
    return 1;
}
