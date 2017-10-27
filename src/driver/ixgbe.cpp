#include "ixgbe.hpp"
#include "ixgbe_type.h"

// allocated for each rx queue, keeps state for the receive function
struct ixgbe_rx_queue {
    volatile union ixgbe_adv_rx_desc* descriptors;
    struct mempool* mempool;
    uint16_t num_entries;
    // position we are reading from
    uint16_t rx_index;
    // virtual addresses to map descriptors back to their mbuf for freeing
    void* virtual_addresses[];
};

// allocated for each tx queue, keeps state for the transmit function
struct ixgbe_tx_queue {
    volatile union ixgbe_adv_tx_desc* descriptors;
    uint16_t num_entries;
    // position to clean up descriptors that where sent out by the nic
    uint16_t clean_index;
    // position to insert packets for transmission
    uint16_t tx_index;
    // virtual addresses to map descriptors back to their mbuf for freeing
    void* virtual_addresses[];
};

ixgbe::ixgbe(const char* pci_addr, uint16_t rx_queues, uint16_t tx_queues)
        : ixy_driver_base(pci_addr, rx_queues, tx_queues) {
    if (::getuid()) {
        warn("Not running as root, this will probably fail");
    }
    if (rx_queues > ixy::MAX_QUEUES) {
        error("cannot configure %d rx queues: limit is %d", rx_queues, ixy::MAX_QUEUES);
    }
    if (tx_queues > ixy::MAX_QUEUES) {
        error("cannot configure %d tx queues: limit is %d", tx_queues, ixy::MAX_QUEUES);
    }
    addr = ::pci_map_resource(pci_addr);
    this->rx_queues = ::calloc(rx_queues, sizeof(struct ixgbe_rx_queue) + sizeof(void*) * ixgbe_driver::MAX_RX_QUEUE_ENTRIES);
    this->tx_queues = ::calloc(rx_queues, sizeof(struct ixgbe_tx_queue) + sizeof(void*) * ixgbe_driver::MAX_TX_QUEUE_ENTRIES);
    reset_and_init();
}

std::uint32_t ixgbe::get_link_speed() const {
    uint32_t links = get_reg32(IXGBE_LINKS);
    if (!(links & IXGBE_LINKS_UP)) {
        return 0;
    }
    switch (links & IXGBE_LINKS_SPEED_82599) {
    case IXGBE_LINKS_SPEED_100_82599:
        return 100;
    case IXGBE_LINKS_SPEED_1G_82599:
        return 1000;
    case IXGBE_LINKS_SPEED_10G_82599:
        return 10000;
    default:
        return 0;
    }
}

void ixgbe::set_promisc(bool enabled) {
    if (enabled) {
        info("enabling promisc mode");
        set_flags32(IXGBE_FCTRL, IXGBE_FCTRL_MPE | IXGBE_FCTRL_UPE);
    } else {
        info("disabling promisc mode");
        clear_flags32(IXGBE_FCTRL, IXGBE_FCTRL_MPE | IXGBE_FCTRL_UPE);
    }
}

#define inc_and_wrap_ring(index, ring_size) (uint16_t) ((index + 1) & (ring_size - 1))

struct pkt_buf* ixgbe::rx_packet(std::uint16_t queue_id) {
    struct ixgbe_rx_queue* queue = ((struct ixgbe_rx_queue*)(rx_queues)) + queue_id;
    uint16_t rx_index = queue->rx_index;
    // rx descriptors are explained in 7.1.5
    volatile union ixgbe_adv_rx_desc* desc_ptr = queue->descriptors + rx_index;
    uint32_t status = desc_ptr->wb.upper.status_error;
    if (status & IXGBE_RXDADV_STAT_DD) {
        if (!(status & IXGBE_RXDADV_STAT_EOP)) {
            error("multi-segment packets are not supported - increase buffer size or decrease MTU");
        }
        // got a packet, read and copy the whole descriptor
        //volatile union ixgbe_adv_rx_desc desc = *desc_ptr;
        struct pkt_buf* buf = (struct pkt_buf*) queue->virtual_addresses[rx_index];
        buf->size = desc_ptr->wb.upper.length;
        // this would be the place to implement RX offloading by translating the device-specific flags
        // to an independent representation in the buf (similiar to how DPDK works)
        // need a new mbuf for the descriptor
        struct pkt_buf* new_buf = pkt_buf_alloc(queue->mempool);
        if (!new_buf) {
            // we could handle empty mempools more gracefully here, but it would be quite messy...
            // make your mempools large enough
            error("failed to allocate new mbuf for rx, you are either leaking memory or your mempool is too small");
        }
        // reset the descriptor
        desc_ptr->read.pkt_addr = new_buf->buf_addr_phy + offsetof(struct pkt_buf, data);
        desc_ptr->read.hdr_addr = 0; // this resets the flags
        // want to read the next one next time
        queue->rx_index = inc_and_wrap_ring(rx_index, queue->num_entries);
        // tell hardware that we are done
        // this is intentionally off by one, otherwise we'd set RDT=RDH if we are receiving faster than packets
        // coming in -- RDT=RDH means queue is full
        // optimization opportunity: don't do this every time, it's slow
        set_reg32(IXGBE_RDT(queue_id), rx_index);
        return buf;
    }
    return NULL;
}

uint16_t ixgbe::tx_packet(uint16_t queue_id, struct pkt_buf* buf) {
    struct ixgbe_tx_queue* queue = ((struct ixgbe_tx_queue*)(tx_queues)) + queue_id;
    // the descriptor is explained in section 7.2.3.2.4
    // we just use a struct copy & pasted from intel, but it basically has two format (hence a union):
    // 1. the write-back format which is written by the NIC once sending it is finished this is used in step 1
    // 2. the read format which is read by the NIC and written by us, this is used in step 2

    uint16_t clean_index = queue->clean_index;
    uint16_t cur_index = queue->tx_index;

    // step 1: clean up descriptors that were sent out by the hardware and return them to the mempool
    // start by reading step 2 which is done first for each packet
    // performance improvement opportunity: don't do this on every call, see DPDK driver for an example
    // this currently requires 30% of the total time, but it will be less bad when we have a batch API
    while (clean_index != cur_index) {
        volatile union ixgbe_adv_tx_desc* txd = queue->descriptors + clean_index;
        uint32_t status = txd->wb.status;
        // hardware sets this flag as soon as it's sent out, we can give it back to the mempool
        if (status & IXGBE_ADVTXD_STAT_DD) {
            struct pkt_buf* buf = (struct pkt_buf*) queue->virtual_addresses[clean_index];
            pkt_buf_free(buf);
            clean_index = inc_and_wrap_ring(clean_index, queue->num_entries);
        } else {
            // reached an unsent descriptor, can't continue cleaning
            break;
        }
    }
    queue->clean_index = clean_index;

    // step 2: send out our packet, if possible
    // we are full if the next index is the one we are trying to reclaim
    if (clean_index == inc_and_wrap_ring(cur_index, queue->num_entries)) {
        return 0;
    }
    // remember virtual address to clean it up later
    queue->virtual_addresses[cur_index] = (void*) buf;
    queue->tx_index = inc_and_wrap_ring(queue->tx_index, queue->num_entries );
    volatile union ixgbe_adv_tx_desc* txd = queue->descriptors + cur_index;
    // NIC reads from here
    txd->read.buffer_addr = buf->buf_addr_phy + offsetof(struct pkt_buf, data);
    // always the same flags: one buffer (EOP), advanced data descriptor, CRC offload, data length
    txd->read.cmd_type_len =
            IXGBE_ADVTXD_DCMD_EOP | IXGBE_ADVTXD_DCMD_RS | IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_DATA | buf->size;
    // no fancy offloading stuff - only the total payload length
    txd->read.olinfo_status = buf->size << IXGBE_ADVTXD_PAYLEN_SHIFT;
    // send out by advancing tail
    set_reg32(IXGBE_TDT(queue_id), queue->tx_index);
    return 1;
}

void ixgbe::do_read_stats(ixy::device_stats* stats) {
    uint32_t rx_pkts = get_reg32(IXGBE_GPRC);
    uint32_t tx_pkts = get_reg32(IXGBE_GPTC);
    uint64_t rx_bytes = get_reg32(IXGBE_GORCL) + (((uint64_t) get_reg32(IXGBE_GORCH)) << 32);
    uint64_t tx_bytes = get_reg32(IXGBE_GOTCL) + (((uint64_t) get_reg32(IXGBE_GOTCH)) << 32);
    if (stats) {
        stats->rx_pkts += rx_pkts;
        stats->tx_pkts += tx_pkts;
        stats->rx_bytes += rx_bytes;
        stats->tx_bytes += tx_bytes;
    }
}

void ixgbe::init_link() {
    // should already be set by the eeprom config, maybe we shouldn't override it here to support weirdo nics?
    set_reg32((get_reg32(0) & ~IXGBE_AUTOC_LMS_MASK) | IXGBE_AUTOC_LMS_10G_SERIAL, 0);
    set_reg32(IXGBE_AUTOC, (get_reg32(IXGBE_AUTOC) & ~IXGBE_AUTOC_10G_PMA_PMD_MASK) | IXGBE_AUTOC_10G_XAUI);
    // negotiate link
    set_flags32(IXGBE_AUTOC, IXGBE_AUTOC_AN_RESTART);
    // datasheet wants us to wait for the link here, but we can continue and wait afterwards
}

void ixgbe::reset_and_init() {
    info("Resetting device %s", addr);
    // section 4.6.3.1 - disable all interrupts
    set_reg32(IXGBE_EIMC, 0x7FFFFFFF);

    // section 4.6.3.2
    set_reg32(IXGBE_CTRL, IXGBE_CTRL_RST_MASK);
    wait_clear_reg32(IXGBE_CTRL, IXGBE_CTRL_RST_MASK);
    usleep(10000);

    // section 4.6.3.1 - disable interrupts again after reset
    set_reg32(IXGBE_EIMC, 0x7FFFFFFF);

    info("Initializing device %s", addr);

    // section 4.6.3 - Wait for EEPROM auto read completion
    wait_set_reg32(IXGBE_EEC, IXGBE_EEC_ARD);

    // section 4.6.3 - Wait for DMA initialization done (RDRXCTL.DMAIDONE)
    wait_set_reg32(IXGBE_RDRXCTL, IXGBE_RDRXCTL_DMAIDONE);

    // section 4.6.4 - initialize link (auto negotiation)
    init_link();

    // section 4.6.5 - statistical counters
    // reset-on-read registers, just read them once
    do_read_stats(NULL);

    // section 4.6.7 - init rx
    init_rx();

    // section 4.6.8 - init tx
    init_tx();

    // enables queues after initializing everything
    for (uint16_t i = 0; i < num_rx_queues; i++) {
        start_rx_queue(i);
    }
    for (uint16_t i = 0; i < num_tx_queues; i++) {
        start_tx_queue(i);
    }

    // skip last step from 4.6.3 - don't want interrupts
    // finally, enable promisc mode by default, it makes testing less annoying
    set_promisc(true);

    // wait for some time for the link to come up
    wait_for_link();
}

void ixgbe::start_rx_queue(int queue_id) {
    debug("starting rx queue %d", queue_id);
    struct ixgbe_rx_queue* queue = ((struct ixgbe_rx_queue*)(rx_queues)) + queue_id;
    // 2048 as pktbuf size is strictly speaking incorrect:
    // we need a few headers (1 cacheline), so there's only 1984 bytes left for the device
    // but the 82599 can only handle sizes in increments of 1 kb; but this is fine since our max packet size
    // is the default MTU of 1518
    // this has to be fixed if jumbo frames are to be supported
    // mempool should be >= the number of rx and tx descriptors for a forwarding application
    queue->mempool = memory_allocate_mempool(4096, 2048);
    if (queue->num_entries & (queue->num_entries - 1)) {
        error("number of queue entries must be a power of 2");
    }
    for (int i = 0; i < queue->num_entries; i++) {
        volatile union ixgbe_adv_rx_desc* rxd = queue->descriptors + i;
        struct pkt_buf* buf = pkt_buf_alloc(queue->mempool);
        if (!buf) {
            error("failed to allocate rx descriptor");
        }
        rxd->read.pkt_addr = buf->buf_addr_phy;
        rxd->read.hdr_addr = 0;
        // we need to return the virtual address in the rx function which the descriptor doesn't know by default
        queue->virtual_addresses[i] = buf;
    }
    // enable queue and wait if necessary
    set_flags32(IXGBE_RXDCTL(queue_id), IXGBE_RXDCTL_ENABLE);
    wait_set_reg32(IXGBE_RXDCTL(queue_id), IXGBE_RXDCTL_ENABLE);
    // rx queue starts out full
    set_reg32(IXGBE_RDH(queue_id), 0);
    // was set to 0 before in the init function
    set_reg32(IXGBE_RDT(queue_id), queue->num_entries - 1);
}

void ixgbe::init_rx() {
    // make sure that rx is disabled while re-configuring it
    // the datasheet also wants us to disable some crypto-offloading related rx paths (but we don't care about them)
    clear_flags32(IXGBE_RXCTRL, IXGBE_RXCTRL_RXEN);
    // no fancy dcb or vt, just a single 128kb packet buffer for us
    set_reg32(IXGBE_RXPBSIZE(0), IXGBE_RXPBSIZE_128KB);
    for (int i = 1; i < 8; i++) {
        set_reg32(IXGBE_RXPBSIZE(i), 0);
    }

    // always enable CRC offloading
    set_flags32(IXGBE_HLREG0, IXGBE_HLREG0_RXCRCSTRP);
    set_flags32(IXGBE_RDRXCTL, IXGBE_RDRXCTL_CRCSTRIP);

    // accept broadcast packets
    set_flags32(IXGBE_FCTRL, IXGBE_FCTRL_BAM);

    // per-queue config, same for all queues
    for (uint16_t i = 0; i < num_rx_queues; i++) {
        debug("initializing rx queue %d", i);
        // enable advanced rx descriptors, we could also get away with legacy descriptors, but they aren't really easier
        set_reg32(IXGBE_SRRCTL(i), (get_reg32(IXGBE_SRRCTL(i)) & ~IXGBE_SRRCTL_DESCTYPE_MASK) | IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF);
        // drop_en causes the nic to drop packets if no rx descriptors are available instead of buffering them
        // a single overflowing queue can fill up the whole buffer and impact operations if not setting this flag
        set_flags32(IXGBE_SRRCTL(i), IXGBE_SRRCTL_DROP_EN);
        // setup descriptor ring, see section 7.1.9
        uint32_t ring_size_bytes = ixgbe_driver::NUM_RX_QUEUE_ENTRIES * sizeof(union ixgbe_adv_rx_desc);
        struct dma_memory mem = memory_allocate_dma(ring_size_bytes);
        // neat trick from Snabb: initialize to 0xFF to prevent rogue memory accesses on premature DMA activation
        memset(mem.virt, -1, ring_size_bytes);
        set_reg32(IXGBE_RDBAL(i), (uint32_t) (mem.phy & 0xFFFFFFFFull));
        set_reg32(IXGBE_RDBAH(i), (uint32_t) (mem.phy >> 32));
        set_reg32(IXGBE_RDLEN(i), ring_size_bytes);
        debug("rx ring %d phy addr:  0x%012lX", i, mem.phy);
        debug("rx ring %d virt addr: 0x%012lX", i, (uintptr_t) mem.virt);
        // set ring to empty at start
        set_reg32(IXGBE_RDH(i), 0);
        set_reg32(IXGBE_RDT(i), 0);
        // private data for the driver, 0-initialized
        struct ixgbe_rx_queue* queue = ((struct ixgbe_rx_queue*)(rx_queues)) + i;
        queue->num_entries = ixgbe_driver::NUM_RX_QUEUE_ENTRIES;
        queue->rx_index = 0;
        queue->descriptors = (union ixgbe_adv_rx_desc*) mem.virt;
    }

    // last step is to set some magic bits mentioned in the last sentence in 4.6.7
    set_flags32(IXGBE_CTRL_EXT, IXGBE_CTRL_EXT_NS_DIS);
    // this flag probably refers to a broken feature: it's reserved and initialized as '1' but it must be set to '0'
    // there isn't even a constant in ixgbe_types.h for this flag
    for (uint16_t i = 0; i < num_rx_queues; i++) {
        clear_flags32(IXGBE_DCA_RXCTRL(i), 1 << 12);
    }

    // start RX
    set_flags32(IXGBE_RXCTRL, IXGBE_RXCTRL_RXEN);
}

void ixgbe::init_tx() {
// crc offload and small packet padding
    set_flags32(IXGBE_HLREG0, IXGBE_HLREG0_TXCRCEN | IXGBE_HLREG0_TXPADEN);

    // set default buffer size allocations
    // see also: section 4.6.11.3.4, no fancy features like DCB and VTd
    set_reg32(IXGBE_TXPBSIZE(0), IXGBE_TXPBSIZE_40KB);
    for (int i = 1; i < 8; i++) {
        set_reg32(IXGBE_TXPBSIZE(i), 0);
    }
    // required when not using DCB/VTd
    set_reg32(IXGBE_DTXMXSZRQ, 0xFFFF);
    clear_flags32(IXGBE_RTTDCS, IXGBE_RTTDCS_ARBDIS);

    // per-queue config for all queues
    for (uint16_t i = 0; i < num_rx_queues; i++) {
        debug("initializing tx queue %d", i);

        // setup descriptor ring, see section 7.1.9
        uint32_t ring_size_bytes = ixgbe_driver::NUM_TX_QUEUE_ENTRIES * sizeof(union ixgbe_adv_tx_desc);
        struct dma_memory mem = memory_allocate_dma(ring_size_bytes);
        memset(mem.virt, -1, ring_size_bytes);
        set_reg32(IXGBE_TDBAL(i), (uint32_t) (mem.phy & 0xFFFFFFFFull));
        set_reg32(IXGBE_TDBAH(i), (uint32_t) (mem.phy >> 32));
        set_reg32(IXGBE_TDLEN(i), ring_size_bytes);
        debug("tx ring %d phy addr:  0x%012lX", i, mem.phy);
        debug("tx ring %d virt addr: 0x%012lX", i, (uintptr_t) mem.virt);

        // descriptor writeback magic values, defaults from DPDK
        // no fancy prefetching, it's also disabled by default in DPDK
        uint32_t txdctl = get_reg32(IXGBE_TXDCTL(i));
        txdctl &= ~(0x3F);
        txdctl |= 32;
        set_reg32(IXGBE_TXDCTL(i), txdctl);

        // private data for the driver, 0-initialized
        struct ixgbe_tx_queue* queue = ((struct ixgbe_tx_queue*)(tx_queues)) + i;
        queue->num_entries = ixgbe_driver::NUM_TX_QUEUE_ENTRIES;
        queue->descriptors = (union ixgbe_adv_tx_desc*) mem.virt;
    }
    // final step: enable DMA
    set_reg32(IXGBE_DMATXCTL, IXGBE_DMATXCTL_TE);
}

void ixgbe::start_tx_queue(int queue_id) {
    debug("starting tx queue %d", queue_id);
    struct ixgbe_tx_queue* queue = ((struct ixgbe_tx_queue*)(tx_queues)) + queue_id;
    if (queue->num_entries & (queue->num_entries - 1)) {
        error("number of queue entries must be a power of 2");
    }
    // tx queue starts out empty
    set_reg32(IXGBE_TDH(queue_id), 0);
    set_reg32(IXGBE_TDT(queue_id), 0);
    // enable queue and wait if necessary
    set_flags32(IXGBE_TXDCTL(queue_id), IXGBE_TXDCTL_ENABLE);
    wait_set_reg32(IXGBE_TXDCTL(queue_id), IXGBE_TXDCTL_ENABLE);
}

void ixgbe::wait_for_link() const {
    info("Waiting for link...");
    int32_t max_wait = 10000000; // 10 seconds in us
    uint32_t poll_interval = 100000; // 10 ms in us
    uint32_t speed;
    while (!(speed = get_link_speed()) && max_wait > 0) {
        usleep(poll_interval);
        max_wait -= poll_interval;
    }
    info("Link speed is %d Mbit/s", get_link_speed());
}
