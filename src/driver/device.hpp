#ifndef IXY_DEVICE_HPP
#define IXY_DEVICE_HPP

#include <cstdint>
#include <unistd.h>

#include "log.h"

namespace ixy {
    constexpr int MAX_QUEUES = 64;
}

template<typename T>
class ixy_driver_base {
protected:
    explicit ixy_driver_base(const char* pci_addr, uint16_t rx_queues, uint16_t tx_queues)
            : num_rx_queues(rx_queues),
              num_tx_queues(tx_queues),
              pci_addr(strdup(pci_addr)) {}

public:
    const char* driver_name = "";
    const std::uint16_t num_rx_queues = 0;
    const std::uint16_t num_tx_queues = 0;
    const char* pci_addr;
    std::uint8_t* addr;
    // allow drivers to keep some state for queues, opaque pointer cast by the driver
    void* rx_queues;
    void* tx_queues;

public:
    std::uint32_t _get_link_speed() const {
        return impl().do_get_link_speed();
    }

    void _set_promisc(bool enabled) {
        return impl().do_set_promisc();
    }

    struct pkt_buf* _rx_packet(std::uint16_t queue_id) {
        return impl().do_rx_packet(queue_id);
    }

    std::uint16_t _tx_packet(std::uint16_t queue_id, struct pkt_buf* buf) {
        return impl().do_tx_packet(queue_id, buf);
    }

    void _read_stats(struct device_stats* stats) {
        return impl().do_read_stats(stats);
    }

    void set_reg32(int reg, std::uint32_t value) {
        __asm__ volatile ("" : : : "memory");
        *((volatile uint32_t*) (addr + reg)) = value;
    }

    std::uint32_t get_reg32(int reg) const {
        __asm__ volatile ("" : : : "memory");
        return *((volatile uint32_t*) (addr + reg));
    }

    void set_flags32(int reg, std::uint32_t flags) {
        set_reg32(reg, get_reg32(reg) | flags);
    }

    void clear_flags32(int reg, uint32_t flags) {
        set_reg32(reg, get_reg32(reg) & ~flags);
    }

    void wait_clear_reg32(int reg, uint32_t mask) {
        __asm__ volatile ("" : : : "memory");
        uint32_t cur = 0;
        while (cur = *((volatile uint32_t*) (addr + reg)), (cur & mask) != 0) {
            debug("waiting for flags 0x%08X in register 0x%05X to clear, current value 0x%08X", mask, reg, cur);
            usleep(10000);
            __asm__ volatile ("" : : : "memory");
            __sync_synchronize();
        }
    }

    void wait_set_reg32(int reg, uint32_t mask) {
        __asm__ volatile ("" : : : "memory");
        uint32_t cur = 0;
        while (cur = *((volatile uint32_t*) (addr + reg)), (cur & mask) != mask) {
            debug("waiting for flags 0x%08X in register 0x%05X, current value 0x%08X", mask, reg, cur);
            usleep(10000);
            __asm__ volatile ("" : : : "memory");
        }
    };

private:
    T& impl() {
        return static_cast<T&>(*this);
    }

    const T& impl() const {
        return static_cast<const T&>(*this);
    }
};

#endif //IXY_DEVICE_HPP
