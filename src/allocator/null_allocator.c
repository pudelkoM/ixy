#include "allocator.h"
#include "allocator_common.h"
#include "log.h"

struct null_allocator {
    struct allocator vfs;
};

static struct mem_blk null_allocator_allocate(struct allocator* a, size_t size) {
    return (struct mem_blk) {NULL, 0};
}

static void null_allocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    if (blk->ptr || blk->size)
        error("Attempting to free not NULL blk with null allocator");
}

static bool null_allocator_owns(struct allocator* a, const struct mem_blk* blk) {
    return false;
}

struct allocator null_allocator_t = {
        1u,
        null_allocator_allocate,
        null_allocator_deallocate,
        null_allocator_owns
};

struct allocator* null_allocator_new() {
    struct null_allocator* na = malloc(sizeof(*na));
    memcpy(&na->vfs, &null_allocator_t, sizeof(null_allocator_t));
    return &na->vfs;
}

void null_allocator_free(struct allocator* a) {
    struct null_allocator* self = container_of(a, struct null_allocator, vfs);
    free(self);
}
