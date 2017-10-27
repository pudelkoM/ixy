#include <cstdio>

#include "driver/ixgbe.hpp"

template<typename T>
static void print_stats(ixy_driver_base<T>& driver) {
    driver.rx_packet(1);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::printf("%s forwards packets between two ports.\n", argv[0]);
        std::printf("Usage: %s <pci bus id2> <pci bus id1>\n", argv[0]);
        return 1;
    }

    ixgbe dev1 = ixgbe(argv[1], 1, 1);
    ixy_driver_base<ixgbe>& base_dev = dev1;

    std::printf("base: %s ixgbe: %s\n", base_dev.driver_name, dev1.driver_name);

    auto buf = dev1.rx_packet(0);

    //print_stats(dev1);

    //ixy_driver_base d = ixgbe("foo", 1, 1);

}