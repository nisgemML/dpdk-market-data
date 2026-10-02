#pragma once
// include/dpdkmd/market_event.hpp
//
// Minimal market-data event, deliberately shaped like the AddOrder events
// used elsewhere in this portfolio (tick-to-trade, udp-multicast-receiver)
// so the rte_ring-vs-mpsc-queue comparison in bench/ is measuring something
// representative, not an arbitrary payload picked to flatter one queue.

#include <cstdint>

namespace dpdkmd {

struct MarketEvent {
    uint64_t seq{0};
    uint64_t recv_ns{0};
    uint64_t order_ref{0};
    uint32_t price{0};
    uint32_t qty{0};
    char     side{' '};
    char     symbol[7]{};
};
static_assert(sizeof(MarketEvent) == 40, "keep this comparable to the 40-byte LogEntry used elsewhere");

} // namespace dpdkmd
