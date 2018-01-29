#include "memory.h"
#include "log.h"

#include <stddef.h>
#include <linux/limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>

// translate a virtual address to a physical one via /proc/self/pagemap
static uintptr_t virt_to_phys(void* virt) {
	long pagesize = sysconf(_SC_PAGESIZE);
	int fd = check_err(open("/proc/self/pagemap", O_RDONLY), "getting pagemap");
	// pagemap is an array of pointers for each normal-sized page
	check_err(lseek(fd, (uintptr_t) virt / pagesize * sizeof(uintptr_t), SEEK_SET), "getting pagemap");
	uintptr_t phy = 0;
	check_err(read(fd, &phy, sizeof(phy)), "translating address");
	close(fd);
	if (!phy) {
		error("failed to translate virtual address %p to physical address", virt);
	}
	// bits 0-54 are the page number
	return (phy & 0x7fffffffffffffULL) * pagesize + ((uintptr_t) virt) % pagesize;
}

static bool is_continuous(void* virt, size_t size) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t virt_base = (uintptr_t) virt;
    if (virt_base % (unsigned long) page_size)
        error("memory block does not start at page boundary");
    uintptr_t phys_base = virt_to_phys(virt);
    for (uintptr_t i = virt_base; i < virt_base + size; i += page_size) {
        uintptr_t phys = virt_to_phys((void*) i);
        intptr_t virt_diff = i - virt_base;
        intptr_t phys_diff = phys - phys_base;
        if (phys_diff != virt_diff) {
            return false;
        }
    }
    return true;
}

// Allocate memory suitable for DMA access in contiguous pages by requesting
// many pages at once and the remapping suitable ones
struct dma_memory memory_allocate_dma(size_t size, bool require_contiguous) {
	long page_size = sysconf(_SC_PAGESIZE);
	const size_t num_pages = 1024;
	const size_t pool_size = num_pages * page_size;
	struct entry {
		void* virt;
		uintptr_t phy;
	};
	struct entry pages[num_pages];

	// Round up to next full page
	if (size % page_size) {
		size = (size - 1 + page_size) & ~(page_size - 1);
	}
	debug("Requested %zu bytes, %zu pages", size, size / page_size);

	// Allocate target area to map our pages into. This is to prevent collisions in the virtual address space during remapping
	void* target = (void*) check_err(mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), "mmap target area");
	debug("Target area %p - %p", target, target + pool_size);

	// Allocate pooling pages
	void* pool = (void*) check_err(mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0), "mmap pool");
	check_err(mlock(pool, pool_size), "disable swap for DMA memory");
	for (size_t i = 0; i < num_pages; ++i) {
		volatile uint8_t temp = ((volatile uint8_t*) pool + i * page_size)[0];
		((volatile uint8_t*) pool + i * page_size)[0] = temp;
		pages[i].virt = pool + i * page_size;
		pages[i].phy = virt_to_phys(pool + i * page_size);
	}
	// Sort by physical address
	for (size_t i = 0; i < num_pages; ++i) {
		for (size_t j = num_pages - 1; j > i; --j) {
			if (pages[j].phy < pages[i].phy) {
				struct entry tmp = pages[i];
				pages[i] = pages[j];
				pages[j] = tmp;
			}
		}
	}
	// Map pages to target area
	for (size_t i = 0; i < num_pages; ++i) {
		pages[i].virt = (void*) check_err(mremap(pages[i].virt, page_size, page_size, MREMAP_MAYMOVE | MREMAP_FIXED, target + i * page_size), "remap pages into target area");
		pages[i].phy = virt_to_phys(pages[i].virt);
	}
	// Find contiguous block
	for (size_t i = 0; i < num_pages - size / page_size; ++i) {
		if (is_continuous(target + i * page_size, size)) {
			debug("success: %p -> 0x%lx", pages[i].virt, pages[i].phy);
			return (struct dma_memory) {
				.virt = pages[i].virt,
				.phy = pages[i].phy
			};
		} else {
			check_err(munmap(target + i * page_size, page_size), "unmap unneeded page");
		}
	}
	// We could not serve the request
	info("Page map:");
	for (size_t i = 0; i < num_pages; ++i) {
		info("#%04zu: %p -> 0x%lx", i, pages[i].virt, pages[i].phy);
	}
	error("Could not find suitable block");
}

// allocate a memory pool from which DMA'able packet buffers can be allocated
// this is currently not yet thread-safe, i.e., a pool can only be used by one thread,
// this means a packet can only be sent/received by a single thread
// entry_size can be 0 to use the default
struct mempool* memory_allocate_mempool(uint32_t num_entries, uint32_t entry_size) {
	entry_size = entry_size ? entry_size : 2048;
	// require entries that neatly fit into the page size, this makes the memory pool much easier
	// otherwise our base_addr + index * size formula would be wrong because we can't cross a page-boundary
	if (HUGE_PAGE_SIZE % entry_size) {
		error("entry size must be a divisor of the huge page size (%d)", HUGE_PAGE_SIZE);
	}
	struct mempool* mempool = (struct mempool*) malloc(sizeof(struct mempool) + num_entries * sizeof(uint32_t));
	struct dma_memory mem = memory_allocate_dma(num_entries * entry_size, false);
	mempool->num_entries = num_entries;
	mempool->buf_size = entry_size;
	mempool->base_addr = mem.virt;
	mempool->free_stack_top = num_entries;
	for (uint32_t i = 0; i < num_entries; i++) {
		mempool->free_stack[i] = i;
		struct pkt_buf* buf = (struct pkt_buf*) (((uint8_t*) mempool->base_addr) + i * entry_size);
		// physical addresses are not contiguous within a pool, we need to get the mapping
		// minor optimization opportunity: this only needs to be done once per page
		buf->buf_addr_phy = virt_to_phys(buf);
		buf->mempool_idx = i;
		buf->mempool = mempool;
		buf->size = 0;
	}
	return mempool;
}

uint32_t pkt_buf_alloc_batch(struct mempool* mempool, struct pkt_buf* bufs[], uint32_t num_bufs) {
	if (mempool->free_stack_top < num_bufs) {
		warn("memory pool %p only has %d free bufs, requested %d", mempool, mempool->free_stack_top, num_bufs);
		num_bufs = mempool->free_stack_top;
	}
	for (uint32_t i = 0; i < num_bufs; i++) {
		uint32_t entry_id = mempool->free_stack[--mempool->free_stack_top];
		bufs[i] = (struct pkt_buf*) (((uint8_t*) mempool->base_addr) + entry_id * mempool->buf_size);
	}
	return num_bufs;
}

struct pkt_buf* pkt_buf_alloc(struct mempool* mempool) {
	struct pkt_buf* buf = NULL;
	pkt_buf_alloc_batch(mempool, &buf, 1);
	return buf;
}

void pkt_buf_free(struct pkt_buf* buf) {
	struct mempool* mempool = buf->mempool;
	mempool->free_stack[mempool->free_stack_top++] = buf->mempool_idx;
}
