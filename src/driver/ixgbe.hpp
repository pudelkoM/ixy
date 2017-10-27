#ifndef IXY_IXGBE_HPP
#define IXY_IXGBE_HPP

#include "device.hpp"
#include "pci.h"
#include "memory.h"
#include "stats.hpp"

namespace ixgbe_driver {
    constexpr int MAX_RX_QUEUE_ENTRIES = 4096;
    constexpr int MAX_TX_QUEUE_ENTRIES = 4096;

    constexpr int NUM_RX_QUEUE_ENTRIES = 1024;
    constexpr int NUM_TX_QUEUE_ENTRIES = 1024;
}

class ixgbe : public ixy_driver_base<ixgbe> {
public:
    const char* driver_name = "ixy-ixgbe";

    explicit ixgbe(const char* pci_addr, uint16_t rx_queues, uint16_t tx_queues);

    std::uint32_t get_link_speed() const;

    void set_promisc(bool enabled);

    struct pkt_buf* rx_packet(std::uint16_t queue_id);

    uint16_t tx_packet(uint16_t queue_id, struct pkt_buf* buf);

    void do_read_stats(ixy::device_stats* stats);

private:
    void init_link();
    void start_rx_queue(int queue_id);
    void start_tx_queue(int queue_id);
    void init_rx();
    void init_tx();
    void wait_for_link() const;
    void reset_and_init();
};

#endif //IXY_IXGBE_HPP
