#ifndef IXY_STATS_HPP
#define IXY_STATS_HPP

#include <cstdint>
#include <cstddef>
#include <time.h>
#include "driver/device.hpp"

namespace ixy {
    struct device_stats {
        const char* pci_addr;
        size_t rx_pkts;
        size_t tx_pkts;
        size_t rx_bytes;
        size_t tx_bytes;

        explicit device_stats(const char* pci_addr = "???");

        void print_stats();

        void print_stats_diff(struct device_stats* stats_old, uint64_t nanos_passed);
    };

    uint64_t monotonic_time();
}

#endif //IXY_STATS_HPP
