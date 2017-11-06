#include "allocator.h"
#include "allocator_common.h"
#include "log.h"

#include <stddef.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>


struct dma_allocator {
    struct allocator vfs;
    void* base_addr;
    uintptr_t base_addr_phy;
};

// translate a virtual address to a physical one via /proc/self/pagemap
static uintptr_t virt_to_phys(void* virt) {
    long pagesize = sysconf(_SC_PAGESIZE);
    int fd = check_err(open("/proc/self/pagemap", O_RDONLY), "getting pagemap");
    // pagemap is an array of pointers for each 4096 byte page
    check_err(lseek(fd, (uintptr_t) virt / pagesize * sizeof(uintptr_t), SEEK_SET), "getting pagemap");
    static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "size mismatch");
    uint64_t phy = 0;
    check_err(read(fd, &phy, sizeof(phy)), "translating address");
    close(fd);
    uint64_t pfn = phy & 0x7fffffffffffffULL;
    if (!phy) {
        debug("page info for %p:\n"
                      "\tpfn: %lu\n"
                      "\tsoft-dirty: %lu\n"
                      "\texclusive: %lu\n"
                      "\tfile-page: %lu\n"
                      "\tswapped: %lu\n"
                      "\tpresent: %lu",
              virt, pfn, (phy >> 54) & 1, (phy >> 56) & 1, (phy >> 61) & 1, (phy >> 62) & 1, (phy >> 63) & 1
        );
        error("failed to translate virtual address %p to physical address", virt);
    }
    // bits 0-54 are the page number
    return pfn * pagesize + ((uintptr_t) virt) % pagesize;
}

static bool is_phys_continuous(struct mem_blk* blk) {
    long pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t virt_base = (uintptr_t) blk->ptr;
    if (virt_base % (unsigned long) pagesize)
        error("memory block does not start at page boundary");
    if (blk->size < (unsigned long) pagesize)
        error("memory block smaller than one page");
    uintptr_t phys_base = virt_to_phys(blk->ptr);
    for (uintptr_t i = virt_base; i < virt_base + blk->size; i+=pagesize) {
        uintptr_t phys = virt_to_phys((void*) i);
        intptr_t virt_diff = i - virt_base;
        intptr_t phys_diff = phys - phys_base;
        if (phys_base + virt_diff != phys) {
            debug("memory block not physically continuous:\n"
                          "\tvirt base %p phys %p\n"
                          "\tvirt end  %p size 0x%zx\n"
                          "\tvirt diff %"PRIiPTR" phys diff %"PRIiPTR"\n"
                          "\tvirt page %p phys %p",
                  (void*) virt_base, (void*) phys_base, blk->ptr + blk->size, blk->size, virt_diff, phys_diff, (void*) i, (void*) phys);
            return false;
        }
    }
    return true;
}

static uint32_t huge_pg_id;

// allocate memory suitable for DMA access in huge pages
// this requires hugetlbfs to be mounted at /mnt/huge
// (not using anonymous hugepages because madvise might fail in subtle ways with some kernel configurations)
// caution: very wasteful when allocating small chunks
// this could be fixed by co-locating allocations on the same page until a request would be too large
static struct mem_blk allocate(struct allocator* a, size_t size) {
    // round up to multiples of 2 MB if necessary, this is the wasteful part
    // when fixing this: make sure to align on 128 byte boundaries (82599 dma requirement)
    if (size % (1 << 21)) {
        size = ((size >> 21) + 1) << 21;
    }
    // C11 stdatomic.h requires a too recent gcc, we want to support gcc 4.8
    uint32_t id = __sync_fetch_and_add(&huge_pg_id, 1);
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/mnt/huge/ixy-%d-%d", getpid(), id);
    // temporary file, will be deleted to prevent leaks of persistent pages
    int fd = check_err(open(path, O_CREAT | O_RDWR, S_IRWXU), "open hugetlbfs file, check that /mnt/huge is mounted");
    check_err(ftruncate(fd, (off_t) size), "allocate huge page memory, check hugetlbfs configuration");
    void* virt_addr = (void*) check_err(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_HUGETLB, fd, 0), "mmap hugepage");
    // never swap out DMA memory
    check_err(mlock(virt_addr, size), "disable swap for DMA memory");
    // don't keep it around in the hugetlbfs
    close(fd);
    unlink(path);
    // touch every page so they are not lazily allocated and virt_to_phys() can resolve their address
    for (void* i = virt_addr; i < virt_addr + size; i+=1u<<21) {
        volatile uint8_t temp = ((volatile uint8_t*)i)[0];
        ((volatile uint8_t*)i)[0] = temp;
    }
    struct mem_blk blk = {
            .ptr = virt_addr,
            .size = size
    };
    if (!is_phys_continuous(&blk))
        error("memory returned by mmap is not physically continuous, try rebooting to clear fragmentation");
    return blk;
}

static void deallocate(struct allocator* a, struct mem_blk* blk) {
    check_err(munmap(blk->ptr, blk->size), "unmapping memory");
}

static bool owns(struct allocator* a, const struct mem_blk* blk) {
    return true;
}

struct allocator dma_allocator_t = {
        4096,
        allocate,
        deallocate,
        owns
};

struct allocator* dma_allocator_new() {
    struct dma_allocator* dma = malloc(sizeof(*dma));
    memcpy(&dma->vfs, &dma_allocator_t, sizeof(dma_allocator_t));
    return &dma->vfs;
}

void dma_allocator_free(struct allocator* a) {
    struct dma_allocator* self = container_of(a, struct dma_allocator, vfs);
    free(self);
}
