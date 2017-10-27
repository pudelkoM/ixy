#include "stats.hpp"

/*
ixy::device_stats::device_stats(ixy_driver_base* dev) {
    rx_pkts = 0;
    tx_pkts = 0;
    rx_bytes = 0;
    tx_bytes = 0;
    device = dev;
    if (dev) {
        dev->do_read_stats(NULL);
    }
}
*/

uint64_t ixy::monotonic_time() {
    struct timespec timespec;
    clock_gettime(CLOCK_MONOTONIC, &timespec);
    return timespec.tv_sec * 1000 * 1000 * 1000 + timespec.tv_nsec;
}

static double diff_mpps(uint64_t pkts_new, uint64_t pkts_old, uint64_t nanos) {
    return (double) (pkts_new - pkts_old) / 1000000.0 / ((double) nanos / 1000000000.0);
}

static uint32_t diff_mbit(uint64_t bytes_new, uint64_t bytes_old, uint64_t pkts_new, uint64_t pkts_old, uint64_t nanos) {
    // take stuff on the wire into account, i.e., the preamble, SFD and IFG (20 bytes)
    // otherwise it won't show up as 10000 mbit/s with small packets which is confusing
    return (uint32_t) (((bytes_new - bytes_old) / 1000000.0 / ((double) nanos / 1000000000.0)) * 8
                       + diff_mpps(pkts_new, pkts_old, nanos) * 20 * 8);
}

ixy::device_stats::device_stats(const char* pci_addr)
        : pci_addr(pci_addr),
          rx_pkts(0),
          tx_pkts(0),
          rx_bytes(0),
          tx_bytes(0) { }

void ixy::device_stats::print_stats() {
    printf("[%s] RX: %zu bytes %zu packets\n", pci_addr, rx_bytes, rx_pkts);
    printf("[%s] TX: %zu bytes %zu packets\n", pci_addr, tx_bytes, tx_pkts);
}

void ixy::device_stats::print_stats_diff(ixy::device_stats* stats_old, uint64_t nanos_passed) {
    printf("[%s] RX: %d Mbit/s %.2f Mpps\n", pci_addr,
           diff_mbit(rx_bytes, stats_old->rx_bytes, rx_pkts, stats_old->rx_pkts, nanos_passed),
           diff_mpps(rx_pkts, stats_old->rx_pkts, nanos_passed)
    );
    printf("[%s] TX: %d Mbit/s %.2f Mpps\n", pci_addr,
           diff_mbit(tx_bytes, stats_old->tx_bytes, tx_pkts, stats_old->tx_pkts, nanos_passed),
           diff_mpps(tx_pkts, stats_old->tx_pkts, nanos_passed)
    );
}
