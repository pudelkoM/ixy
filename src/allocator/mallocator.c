#include "allocator.h"
#include "allocator_common.h"


struct mallocator {
    struct allocator vfs;
};

static struct mem_blk mallocator_allocate(struct allocator* a, size_t size) {
    return (struct mem_blk) {
            malloc(size),
            size
    };
}

static void mallocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    free(blk->ptr);
#if !defined(NDEBUG)
    blk->ptr = NULL;
    blk->size = 0;
#endif
}

static bool mallocator_owns(struct allocator* a, const struct mem_blk* blk) {
    return true;
}

struct allocator mallocator_t = {
        1u,
        mallocator_allocate,
        mallocator_deallocate,
        mallocator_owns
};

struct allocator* mallocator_new() {
    struct mallocator* ma = malloc(sizeof(*ma));
    memcpy(&ma->vfs, &mallocator_t, sizeof(mallocator_t));
    return &ma->vfs;
}

void mallocator_free(struct allocator* a) {
    struct mallocator* self = container_of(a, struct mallocator, vfs);
    free(self);
}

