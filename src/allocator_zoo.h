#ifndef IXY_ALLOCATOR_H
#define IXY_ALLOCATOR_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>


struct mem_blk {
    void* ptr;
    size_t size;
};

struct allocator {
    const unsigned alignment;

    struct mem_blk (* allocate)(struct allocator*, size_t);

    void (* deallocate)(struct allocator*, struct mem_blk*);

    bool (* owns)(struct allocator*, const struct mem_blk*);
};

/**
 * Creates a new stack (LIFO) allocator for fixed sized elements
 * @num_entries Number of slots
 * @entry_size Size of each element
 * @parent Allocator to source memory from
 * @return
 */
struct allocator* stack_allocator_new(uint32_t num_entries, uint32_t entry_size, struct allocator* parent);

void stack_allocator_free(struct allocator* a);

extern struct allocator mallocator_t;

extern struct allocator null_allocator_t;

#endif //IXY_ALLOCATOR_H
