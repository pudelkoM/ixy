#include <stddef.h>
#include "log.h"
#include "allocator.h"
#include "allocator_common.h"
#include "rte_spinlock.h"

struct spinlock_stack_allocator {
    struct allocator vfs;
    struct allocator* parent;
    rte_spinlock_t lock;
    void* base_addr;
    uintptr_t base_addr_phy;
    uint32_t entry_size;
    uint32_t num_entries;
    uint32_t free_stack_top;
    uint32_t free_stack[];
};

static struct mem_blk allocate(struct allocator* a, size_t size) {
    struct spinlock_stack_allocator* self = container_of(a, struct spinlock_stack_allocator, vfs);
    rte_spinlock_lock(&self->lock);
    struct mem_blk blk;
    if (self->free_stack_top == 0 || size > self->entry_size)
        blk = (struct mem_blk) {NULL, 0};
    else {
        uint32_t entry_id = self->free_stack[--self->free_stack_top];
        blk = (struct mem_blk) {((uint8_t*) self->base_addr) + entry_id * self->entry_size, self->entry_size};
    }
    rte_spinlock_unlock(&self->lock);
    return blk;
}

static void deallocate(struct allocator* a, struct mem_blk* blk) {
    static_assert(PTRDIFF_MAX >= UINT32_MAX, "PTRDIFF_MAX < UINT32_MAX");
    struct spinlock_stack_allocator* self = container_of(a, struct spinlock_stack_allocator, vfs);
    if (self->entry_size != blk->size)
        error("Size of returned block (%zu) does not match stack element size (%u)", blk->size, self->entry_size);
    ptrdiff_t entry_id = (blk->ptr - self->base_addr) / self->entry_size;
    if (entry_id > self->num_entries || entry_id < 0)
        error("Calculated entry id (%lu) is outside of stack range", entry_id);
    rte_spinlock_lock(&self->lock);
    self->free_stack[self->free_stack_top++] = (uint32_t) entry_id;
    rte_spinlock_unlock(&self->lock);
#if !defined(NDEBUG)
    blk->ptr = NULL;
    blk->size = 0;
#endif
}

static bool owns(struct allocator* a, const struct mem_blk* blk) {
    struct spinlock_stack_allocator* self = container_of(a, struct spinlock_stack_allocator, vfs);
    return blk->ptr >= self->base_addr && blk->ptr < self->base_addr + self->num_entries * self->entry_size;
}

struct allocator* spinlock_stack_allocator_new(uint32_t num_entries, uint32_t entry_size, struct allocator* parent) {
    entry_size = entry_size ? entry_size : 2048;
    struct allocator a = {
            1u,
            allocate,
            deallocate,
            owns
    };
    struct spinlock_stack_allocator* sa = malloc(sizeof(*sa) + sizeof(uint32_t) * num_entries);
    if (!sa)
        return NULL;
    memcpy(&sa->vfs, &a, sizeof(a));

    sa->parent = parent;
    rte_spinlock_init(&sa->lock);
    struct mem_blk mem = parent->allocate(parent, num_entries * entry_size);
    if (!mem.ptr)
        goto error;
    sa->num_entries = num_entries;
    sa->entry_size = entry_size;
    sa->base_addr = mem.ptr;
    sa->free_stack_top = num_entries;
    for (uint32_t i = 0; i < num_entries; i++) {
        sa->free_stack[i] = i;
    }
    return &sa->vfs;
    error:
    free(sa);
    return NULL;
}

void spinlock_stack_allocator_free(struct allocator* a) {
    struct spinlock_stack_allocator* self = container_of(a, struct spinlock_stack_allocator, vfs);
    rte_spinlock_lock(&self->lock);
    struct mem_blk blk = {self->base_addr, self->num_entries * self->entry_size};
    self->parent->deallocate(self->parent, &blk);
    rte_spinlock_unlock(&self->lock);
    free(self);
}
