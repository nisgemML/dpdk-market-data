// tests/test_afpacket_rxtx.cpp
//
// Real DPDK packet RX/TX -- not just EAL/mempool/ring primitives (see
// test_dpdk_primitives.cpp), actual rte_eth_tx_burst()/rte_eth_rx_burst()
// through a real ethdev port, net_af_packet bound to the loopback
// interface. This was deliberately NOT attempted in the first version of
// this repo (see the original README/LIMITATIONS.md framing) specifically
// to avoid overclaiming; this file closes that gap honestly, with the
// same honest ceiling restated below, not erased.
//
// Mechanism: `lo` reflects outgoing frames back as incoming on the same
// interface, so a single af_packet-bound port can both send and receive
// its own traffic -- no second process or port needed to prove the RX/TX
// path genuinely moves bytes through DPDK's real ethdev API.
//
// What this proves: rte_eth_tx_burst() and rte_eth_rx_burst() genuinely
// move a real Ethernet frame through DPDK's API over a real (if virtual)
// network interface, round-tripping payload bytes correctly.
//
// What this does NOT prove, same honest ceiling as before: real kernel
// bypass. net_af_packet is a *software* PMD -- it still goes through the
// kernel's own AF_PACKET socket path internally. The actual kernel-bypass
// benefit DPDK is built for needs a DMA-capable NIC bound to
// uio_pci_generic/vfio-pci, which this environment does not have. See
// LIMITATIONS.md.

#include "dpdkmd/market_event.hpp"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include <cstdio>
#include <cstring>

namespace {
int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
} while (0)

// Minimal 14-byte Ethernet header. EtherType 0x88B5 is IEEE 802 "local
// experimental" -- deliberately not 0x0800 (IPv4), since this test is
// proving DPDK's L2 RX/TX framing works, not reimplementing a correct
// IP/UDP stack with real header checksums for no added test value.
struct EthHdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype_be;
};
} // namespace

int main(int argc, char** argv) {
    const int ret = rte_eal_init(argc, argv);
    CHECK(ret >= 0);
    if (ret < 0) { std::fprintf(stderr, "EAL init failed\n"); return 1; }

    const uint16_t nb_ports = rte_eth_dev_count_avail();
    CHECK(nb_ports >= 1);
    if (nb_ports < 1) {
        std::fprintf(stderr, "no ports -- run with --vdev=net_af_packet0,iface=lo\n");
        return 1;
    }
    const uint16_t port_id = 0;

    struct rte_mempool* pool = rte_pktmbuf_pool_create("afpkt_pool", 2048, 256, 0,
                                                         RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    CHECK(pool != nullptr);

    struct rte_eth_conf port_conf{};
    CHECK(rte_eth_dev_configure(port_id, 1, 1, &port_conf) == 0);
    CHECK(rte_eth_rx_queue_setup(port_id, 0, 128, rte_eth_dev_socket_id(port_id), nullptr, pool) == 0);
    CHECK(rte_eth_tx_queue_setup(port_id, 0, 128, rte_eth_dev_socket_id(port_id), nullptr) == 0);
    CHECK(rte_eth_dev_start(port_id) == 0);

    // Build one frame: Ethernet header + a real dpdkmd::MarketEvent payload
    // (the same 40-byte struct used in bench_ring_comparison.cpp).
    struct rte_mbuf* tx_mbuf = rte_pktmbuf_alloc(pool);
    CHECK(tx_mbuf != nullptr);

    auto* eth = rte_pktmbuf_mtod(tx_mbuf, EthHdr*);
    std::memset(eth->dst, 0xFF, 6); // broadcast -- simplest valid dst for a loopback test
    std::memset(eth->src, 0x02, 6); // locally-administered, arbitrary
    eth->ethertype_be = rte_cpu_to_be_16(0x88B5);

    dpdkmd::MarketEvent ev{};
    ev.seq = 777;
    ev.order_ref = 123456;
    ev.price = 1000000;
    ev.qty = 50;
    ev.side = 'B';
    std::memcpy(ev.symbol, "AFPKT00", 7);
    std::memcpy(reinterpret_cast<uint8_t*>(eth) + sizeof(EthHdr), &ev, sizeof(ev));

    const uint16_t frame_len = static_cast<uint16_t>(sizeof(EthHdr) + sizeof(ev));
    tx_mbuf->data_len = frame_len;
    tx_mbuf->pkt_len = frame_len;

    const uint16_t sent = rte_eth_tx_burst(port_id, 0, &tx_mbuf, 1);
    CHECK(sent == 1);

    // lo reflects the frame back; poll rx_burst for it. A short retry
    // loop, not a fixed sleep -- same convention used throughout this
    // portfolio for not timing scheduler noise.
    struct rte_mbuf* rx_mbufs[8];
    uint16_t received = 0;
    bool match = false;
    for (int attempt = 0; attempt < 2000 && received == 0; ++attempt) {
        received = rte_eth_rx_burst(port_id, 0, rx_mbufs, 8);
        if (received == 0) rte_delay_us_block(500);
    }
    CHECK(received >= 1);

    if (received >= 1) {
        for (uint16_t i = 0; i < received; ++i) {
            auto* rx_eth = rte_pktmbuf_mtod(rx_mbufs[i], EthHdr*);
            if (rte_be_to_cpu_16(rx_eth->ethertype_be) == 0x88B5 &&
                rx_mbufs[i]->data_len >= sizeof(EthHdr) + sizeof(ev)) {
                dpdkmd::MarketEvent round_tripped{};
                std::memcpy(&round_tripped, reinterpret_cast<uint8_t*>(rx_eth) + sizeof(EthHdr), sizeof(round_tripped));
                if (round_tripped.seq == 777 && round_tripped.order_ref == 123456 &&
                    round_tripped.price == 1000000 && round_tripped.qty == 50 &&
                    round_tripped.side == 'B' && std::memcmp(round_tripped.symbol, "AFPKT00", 7) == 0) {
                    match = true;
                }
            }
            rte_pktmbuf_free(rx_mbufs[i]);
        }
    }
    CHECK(match);

    rte_eth_dev_stop(port_id);
    rte_eal_cleanup();

    if (g_failures == 0) {
        std::printf("PASS: real DPDK ethdev RX/TX over net_af_packet (lo) -- "
                     "frame sent via rte_eth_tx_burst, received via rte_eth_rx_burst, "
                     "payload round-tripped byte-exact. Software PMD, not real kernel "
                     "bypass -- see LIMITATIONS.md.\n");
        return 0;
    }
    std::printf("FAIL: %d check(s) failed\n", g_failures);
    return 1;
}
