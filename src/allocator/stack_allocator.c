#include "allocator.h"
#include "allocator_common.h"

#include "log.h"

struct stack_allocator {
    struct allocator vfs;
    struct allocator* parent;
    void* base_addr;
    uintptr_t base_addr_phy;
    uint32_t entry_size;
    uint32_t num_entries;
    uint32_t free_stack_top;
    uint32_t free_stack[];
};

static struct mem_blk stack_allocator_allocate(struct allocator* a, size_t size) {
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    if (self->free_stack_top == 0 || size > self->entry_size)
        return (struct mem_blk) {NULL, 0};
    uint32_t entry_id = self->free_stack[--self->free_stack_top];
    return (struct mem_blk) {((uint8_t*) self->base_addr) + entry_id * self->entry_size, self->entry_size};
}

static void stack_allocator_deallocate(struct allocator* a, struct mem_blk* blk) {
    static_assert(PTRDIFF_MAX >= UINT32_MAX, "PTRDIFF_MAX < UINT32_MAX");
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    if (self->entry_size != blk->size)
        error("Size of returned block (%zu) does not match stack element size (%u)", blk->size, self->entry_size);
    ptrdiff_t entry_id = (blk->ptr - self->base_addr) / self->entry_size;
    if (entry_id > self->num_entries || entry_id < 0)
        error("Calculated entry id (%lu) is outside of stack range", entry_id);
    self->free_stack[self->free_stack_top++] = (uint32_t) entry_id;
#if !defined(NDEBUG)
    blk->ptr = NULL;
    blk->size = 0;
#endif
}

static bool stack_allocator_owns(struct allocator* a, const struct mem_blk* blk) {
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    return blk->ptr >= self->base_addr && blk->ptr < self->base_addr + self->num_entries * self->entry_size;
}

struct allocator* stack_allocator_new(uint32_t num_entries, uint32_t entry_size, struct allocator* parent) {
    entry_size = entry_size ? entry_size : 2048;
    struct allocator a = {
            1u,
            stack_allocator_allocate,
            stack_allocator_deallocate,
            stack_allocator_owns
    };
    struct stack_allocator* sa = malloc(sizeof(*sa) + sizeof(uint32_t) * num_entries);
    if (!sa)
        return NULL;
    memcpy(&sa->vfs, &a, sizeof(a));

    sa->parent = parent;
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

void stack_allocator_free(struct allocator* a) {
    struct stack_allocator* self = container_of(a, struct stack_allocator, vfs);
    struct mem_blk blk = {self->base_addr, self->num_entries * self->entry_size};
    self->parent->deallocate(self->parent, &blk);
    free(self);
}

