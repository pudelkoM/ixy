#include <cstdio>
#include <memory>
#include <libseccomp_init.h>

#include "stats.hpp"
#include "driver/ixgbe.hpp"


int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::printf("%s forwards packets between two ports.\n", argv[0]);
        std::printf("Usage: %s <pci bus id2> <pci bus id1>\n", argv[0]);
        return 1;
    }

    auto dev1 = new ixgbe(argv[1], 1, 1);
    ixgbe* dev2;
    if (strcmp(argv[1], argv[2])) {
        dev2 = new ixgbe(argv[2], 1, 1);
    } else {
        // same device, cannot be initialized twice
        // this effectively turns this into an echo server
        dev2 = dev1;
    }
    setup_seccomp();


    uint64_t last_stats_printed = ixy::monotonic_time();
    ixy::device_stats stats1(dev1->pci_addr), stats1_old;
    ixy::device_stats stats2(dev2->pci_addr), stats2_old;

    uint64_t counter = 0;

    while (true) {
        struct pkt_buf* buf = dev1->rx_packet(0);
        if (buf) {
            // transmit function takes care of freeing the packet
            dev2->tx_packet(0, buf);
        }

        // don't poll the time unnecessarily
        if ((counter++ & 0xFFF) == 0) {
            uint64_t time = ixy::monotonic_time();
            if (time - last_stats_printed > 1000 * 1000 * 1000) {
                // every second
                dev1->do_read_stats(&stats1);
                stats1.print_stats_diff(&stats1_old, time - last_stats_printed);
                stats1_old = stats1;
                if (dev1 != dev2) {
                    dev2->do_read_stats(&stats2);
                    stats2.print_stats_diff(&stats2_old, time - last_stats_printed);
                    stats2_old = stats2;
                }
                last_stats_printed = time;
            }
        }
    }

}